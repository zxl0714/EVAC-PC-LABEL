"""Read-only ISO9660/PS2 boot inventory for the static-recompilation pipeline."""
import argparse
from dataclasses import asdict, dataclass
import hashlib
import json
from pathlib import Path
import re
import struct

SECTOR = 2048
TARGETS = {'SLPS-25352': 'espgaluda', 'SLPM-66301': 'ibara'}


@dataclass(frozen=True)
class Entry:
    path: str
    lba: int
    size: int
    directory: bool


def both_endian(data, offset, length):
    little = int.from_bytes(data[offset:offset+length], 'little')
    big = int.from_bytes(data[offset+length:offset+length*2], 'big')
    if little != big:
        raise ValueError('ISO9660 endian copies disagree')
    return little


def canonical(name):
    name = name.replace('\\', '/').strip('/')
    parts = name.split('/')
    if not name or any(p in ('', '.', '..') for p in parts) or ':' in name:
        raise ValueError('Invalid disc path')
    return '/'.join(re.sub(r';[0-9]+$', '', p).upper() for p in parts)


class Iso9660:
    """Bounded, seekable 2048-byte-sector reader; never mounts or writes a disc."""
    def __init__(self, stream, size):
        self.stream, self.size = stream, size
        self.volume_size = size
        primary = None
        for sector in range(16, 80):
            descriptor = self.read(sector * SECTOR, SECTOR)
            if descriptor[1:7] != b'CD001\x01':
                raise ValueError('Not a 2048-byte-sector ISO9660 image')
            if descriptor[0] == 1 and primary is None:
                primary = descriptor
            if descriptor[0] == 255:
                break
        else:
            raise ValueError('Missing volume descriptor terminator')
        if primary is None or both_endian(primary, 128, 2) != SECTOR:
            raise ValueError('Missing primary volume or unsupported sector size')
        self.volume_size = both_endian(primary, 80, 4) * SECTOR
        if self.volume_size > size or self.volume_size < 18 * SECTOR:
            raise ValueError('Truncated ISO volume')
        self.volume = primary[40:72].decode('ascii').rstrip(' \0')
        self.root = self.record(primary[156:156+primary[156]], '')
        if not self.root.directory:
            raise ValueError('ISO root is not a directory')

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset + size > self.volume_size:
            raise ValueError('Disc extent is outside the volume')
        self.stream.seek(offset)
        data = self.stream.read(size)
        if len(data) != size:
            raise ValueError('Truncated disc extent')
        return data

    def record(self, data, path):
        if len(data) < 34 or len(data) != data[0] or 33 + data[32] > len(data):
            raise ValueError('Invalid ISO directory record')
        if data[1] or data[26] or data[27] or data[25] & 0x80:
            raise ValueError('Extended, interleaved or multi-extent records are unsupported')
        lba, size = both_endian(data, 2, 4), both_endian(data, 10, 4)
        if lba * SECTOR + size > self.volume_size:
            raise ValueError('Disc record is outside the volume')
        return Entry(path, lba, size, bool(data[25] & 2))

    def entries(self):
        result = {}
        pending = [self.root]
        seen = set()
        while pending:
            parent = pending.pop()
            extent = (parent.lba, parent.size)
            if extent in seen:
                raise ValueError('Repeated or cyclic ISO directory')
            seen.add(extent)
            if parent.size > 16 * 1024 * 1024:
                raise ValueError('ISO directory exceeds inventory limit')
            data = self.read(parent.lba * SECTOR, parent.size)
            offset = 0
            while offset < len(data):
                length = data[offset]
                if not length:
                    offset = (offset // SECTOR + 1) * SECTOR
                    continue
                if length > SECTOR - offset % SECTOR:
                    raise ValueError('Directory record crosses a sector boundary')
                record = data[offset:offset+length]
                entry = self.record(record, '')
                raw = record[33:33+record[32]]
                offset += length
                if raw in (b'\0', b'\1'):
                    continue
                name = raw.decode('ascii')
                if '/' in name or '\\' in name:
                    raise ValueError('Invalid directory identifier')
                path = canonical((parent.path + '/' if parent.path else '') + name)
                entry = Entry(path, entry.lba, entry.size, entry.directory)
                if path in result or len(result) >= 65536 or path.count('/') > 32:
                    raise ValueError('Duplicate path or ISO inventory limit exceeded')
                result[path] = entry
                if entry.directory:
                    pending.append(entry)
        return result

    def contents(self, entry, limit=32*1024*1024):
        if entry.directory or entry.size > limit:
            raise ValueError('Not a file or file exceeds inspection limit')
        return self.read(entry.lba * SECTOR, entry.size)


def boot_path(system):
    match = re.search(r'^\s*BOOT2\s*=\s*cdrom0?:[\\/]+([^\r\n]+)',
                      system.decode('ascii'), flags=re.MULTILINE | re.IGNORECASE)
    if not match:
        raise ValueError('Missing PS2 BOOT2 entry in SYSTEM.CNF')
    return canonical(match[1].strip())


def elf_info(data):
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('PS2 boot file must be a little-endian ELF32')
    kind, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phnum = struct.unpack_from('<HHIIIIIHHH', data, 16)
    if kind != 2 or machine != 8 or version != 1 or ehsize != 52 or phsize != 32 or phnum > 256:
        raise ValueError('Unsupported PS2 ELF header')
    if phoff + phsize * phnum > len(data):
        raise ValueError('Truncated ELF program table')
    segments = []
    for index in range(phnum):
        ptype, offset, address, physical, filesz, memsz, permissions, alignment = struct.unpack_from('<8I', data, phoff+index*phsize)
        if ptype != 1:
            continue
        if filesz > memsz or offset + filesz > len(data) or address + memsz > 0x100000000:
            raise ValueError('Invalid ELF load segment')
        segments.append(dict(address=f'0x{address:08x}', offset=offset, fileSize=filesz,
                             memorySize=memsz, permissions=permissions))
    if not any(s['permissions'] & 1 and int(s['address'], 16) <= entry < int(s['address'], 16)+s['memorySize'] for s in segments):
        raise ValueError('ELF entry point is outside executable segments')
    return dict(entry=f'0x{entry:08x}', flags=f'0x{flags:08x}', segments=segments,
                size=len(data), sha256=hashlib.sha256(data).hexdigest())


def inspect(path, boot_output=None):
    path = Path(path)
    with path.open('rb') as stream:
        disc = Iso9660(stream, path.stat().st_size)
        entries = disc.entries()
        if 'SYSTEM.CNF' not in entries:
            raise ValueError('SYSTEM.CNF is missing from the disc root')
        boot = boot_path(disc.contents(entries['SYSTEM.CNF'], 65536))
        if boot not in entries:
            raise ValueError('BOOT2 file is missing')
        program = disc.contents(entries[boot])
        elf = elf_info(program)
        serial_match = re.fullmatch(r'([A-Z]{4})_(\d{3})\.(\d{2})', boot)
        serial = f'{serial_match[1]}-{serial_match[2]}{serial_match[3]}' if serial_match else ''
        if boot_output:
            # Development extraction is explicit and exclusive. Player preparation
            # will use its own fingerprinted cache, never the original data root.
            output = Path(boot_output)
            if output.resolve() == path.resolve():
                raise ValueError('Boot output overlaps the source ISO')
            output.parent.mkdir(parents=True, exist_ok=True)
            with output.open('xb') as target:
                target.write(program)
        return dict(format=1, volume=disc.volume, discSize=path.stat().st_size,
                    serial=serial, candidateGame=TARGETS.get(serial), boot=boot, elf=elf,
                    files=[asdict(e) for e in sorted(entries.values(), key=lambda e: e.path)])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso', type=Path)
    parser.add_argument('--extract-boot', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = inspect(args.iso, args.extract_boot)
    text = json.dumps(result, indent=2, ensure_ascii=False) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open('x', encoding='utf-8', newline='\n') as output:
            output.write(text)
    else:
        print(text, end='')
