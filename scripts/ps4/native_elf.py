"""Bounded readers for the decrypted ELF inputs used by the native runtime."""

import base64
import hashlib
import struct
from collections import Counter
from pathlib import Path

LOAD, RELRO, DYNAMIC, DYNLIBDATA = 1, 0x61000010, 2, 0x61000000
STRTAB, STRSZ, SYMTAB, SYMSZ = 0x61000035, 0x61000037, 0x61000039, 0x6100003F
ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-"


def plain_self_elf(raw):
    """Read only plain, uncompressed SELF segments. No keys or decryption.

    Layout reference: shadPS4 core/loader/elf.{h,cpp}. The reconstructed view
    contains ELF headers and file-backed program segments, not a signed SELF.
    """
    if raw[:4] != bytes.fromhex('4f153d1d'):
        return raw
    def require(okay):
        if not okay:
            raise ValueError('Unsupported or malformed plaintext SELF')
    require(32 <= len(raw) <= 512 * 1024**2 and raw[4:10] == bytes((0,1,1,0x12,1,1)))
    declared, = struct.unpack_from('<Q',raw,16)
    count, = struct.unpack_from('<H',raw,24)
    header_bytes, meta_bytes = struct.unpack_from('<HH',raw,12)
    embedded=32+count*32
    require(1<=count<=64 and embedded+64<=header_bytes and header_bytes+meta_bytes<=declared<=len(raw))
    header=struct.unpack_from('<16sHHIQQQIHHHHHH',raw,embedded)
    require(header[0][:7]==b'\x7fELF\x02\x01\x01' and header[9]==56 and 1<=header[10]<=64 and 64<=header[5]<=65536)
    table_end=header[5]+56*header[10]
    require(embedded+table_end<=header_bytes)
    programs=[struct.unpack_from('<II6Q',raw,embedded+header[5]+56*i) for i in range(header[10])]
    extent=max(table_end,*(p[2]+p[5] for p in programs))
    require(extent<=512*1024**2)
    result=bytearray(extent)
    result[:table_end]=raw[embedded:embedded+table_end]
    ranges=[(0,table_end)]
    selected=set()
    for index in range(count):
        flags,offset,size,memory=struct.unpack_from('<4Q',raw,32+index*32)
        require(not flags & 10 and offset>=header_bytes+meta_bytes and offset<=declared and size<=declared-offset)
        if not flags & 0x800:
            continue
        program=(flags>>20)&0xfff
        require(program<len(programs) and program not in selected)
        selected.add(program)
        _,_,target,_,_,length,_,_=programs[program]
        require(size==memory==length and target<=extent and length<=extent-target)
        body=raw[offset:offset+size]
        for began,ended in ranges:
            start,stop=max(began,target),min(ended,target+length)
            require(stop<=start or result[start:stop]==body[start-target:stop-target])
        result[target:target+length]=body
        ranges.append((target,target+length))
    # Some plain module dumps append the SCE version-note bytes after the
    # declared SELF extent. Accept only one exact, bounded note, not arbitrary
    # missing load/dynamic data or a guessed offset into the original ELF.
    notes=[p for p in programs if p[0]==0x6fffff01 and p[5]]
    if len(raw)>declared:
        require(len(notes)==1 and 0<len(raw)-declared<=4096 and notes[0][5]==len(raw)-declared)
        target,length=notes[0][2],notes[0][5]
        require(all(target+length<=start or target>=stop for start,stop in ranges))
        result[target:target+length]=raw[declared:]
        ranges.append((target,target+length))
    for program in programs:
        cursor,stop=program[2],program[2]+program[5]
        for start,end in sorted(ranges):
            if start<=cursor:
                cursor=max(cursor,end)
        require(cursor>=stop)
    return bytes(result)


def nid(name):
    salt = bytes.fromhex("518d64a635ded8c1e6b039b1c3e55230")
    digest = hashlib.sha1(name.encode("utf-8") + salt).digest()[:8][::-1]
    return base64.b64encode(digest, altchars=b"+-").decode("ascii").rstrip("=")


def encode_id(value):
    result = ALPHABET[value & 63]
    value >>= 6
    while value:
        result = ALPHABET[value & 63] + result
        value >>= 6
    return result


class Elf:
    def __init__(self, raw):
        self.raw = raw
        header = self.unpack("<16sHHIQQQIHHHHHH", 0)
        if header[0][:7] != b"\x7fELF\x02\x01\x01" or header[2] != 62:
            raise ValueError("Expected decrypted ELF64 little-endian x86-64 input")
        if header[1] not in (0xFE10, 0xFE18):
            raise ValueError("Unexpected executable type")
        if header[8] != 64 or header[9] != 56 or not 1 <= header[10] <= 64:
            raise ValueError("Invalid ELF/program header sizes")
        self.entry, self.elf_type = header[4], header[1]
        self.headers = [self.unpack("<II6Q", header[5] + i * 56) for i in range(header[10])]
        self.segments = []
        for kind, flags, offset, address, _, file_size, memory_size, alignment in self.headers:
            self.slice(offset, file_size)
            if kind not in (LOAD, RELRO):
                continue
            if file_size > memory_size or memory_size == 0 or address + memory_size > 512 * 1024**2:
                raise ValueError("Invalid load segment extent")
            if flags & ~7 or alignment and alignment & (alignment - 1):
                raise ValueError("Invalid segment flags/alignment")
            self.segments.append(dict(address=address, file_offset=offset, file_size=file_size,
                                      memory_size=memory_size, flags=flags, alignment=alignment))
        self.segments.sort(key=lambda s: s["address"])
        if not self.segments:
            raise ValueError("No load segments")
        for previous, current in zip(self.segments, self.segments[1:]):
            if previous["address"] + previous["memory_size"] > current["address"]:
                raise ValueError("Overlapping load segments")
        self.image_size = max(s["address"] + s["memory_size"] for s in self.segments)
        if self.entry and not self.contains(self.entry, 1, executable=True):
            raise ValueError("Entry is outside executable segments")
        self.procparam = next((p[3] for p in self.headers if p[0] == 0x61000001), 0)
        dynamic = self.one_header(DYNAMIC)
        self.dynamic = []
        if dynamic[5] % 16:
            raise ValueError("Misaligned dynamic table")
        for offset in range(dynamic[2], dynamic[2] + dynamic[5], 16):
            item = self.unpack("<qQ", offset)
            if item[0] == 0:
                break
            self.dynamic.append(item)
        else:
            raise ValueError("Unterminated dynamic table")
        self.tags = dict(self.dynamic)
        self.dyn_data = self.one_header(DYNLIBDATA)
        self.strings = self.dynamic_slice(self.tags[STRTAB], self.tags[STRSZ])
        sym_data = self.dynamic_slice(self.tags[SYMTAB], self.tags[SYMSZ])
        if len(sym_data) % 24 or self.tags.get(0x6100003B, 24) != 24:
            raise ValueError("Invalid symbol table shape")
        self.symbols = []
        for index in range(0, len(sym_data), 24):
            name, info, other, section, value, size = struct.unpack_from("<IBBHQQ", sym_data, index)
            self.symbols.append(dict(name=self.string(name), type=info & 15, binding=info >> 4,
                                     other=other, section=section, value=value, size=size))
        self.relocations = []
        for table, size in ((0x6100002F, 0x61000031), (0x61000029, 0x6100002D)):
            block = self.dynamic_slice(self.tags.get(table, 0), self.tags.get(size, 0))
            if len(block) % 24:
                raise ValueError("Invalid relocation table shape")
            for offset in range(0, len(block), 24):
                address, info, addend = struct.unpack_from("<QQq", block, offset)
                symbol = info >> 32
                if symbol >= len(self.symbols) or not self.contains(address, 8):
                    raise ValueError("Relocation lies outside mapped segments or symbol table")
                self.relocations.append(dict(address=address, type=info & 0xFFFFFFFF,
                                             symbol=symbol, addend=addend))

    def slice(self, offset, size):
        if offset < 0 or size < 0 or offset > len(self.raw) or size > len(self.raw) - offset:
            raise ValueError("File extent is out of bounds")
        return self.raw[offset:offset + size]

    def unpack(self, layout, offset):
        return struct.unpack(layout, self.slice(offset, struct.calcsize(layout)))

    def one_header(self, kind):
        matches = [p for p in self.headers if p[0] == kind]
        if len(matches) != 1:
            raise ValueError(f"Expected one program header of type {kind:#x}")
        return matches[0]

    def dynamic_slice(self, offset, size):
        if offset > self.dyn_data[5] or size > self.dyn_data[5] - offset:
            raise ValueError("Dynamic table extent is out of bounds")
        return self.slice(self.dyn_data[2] + offset, size)

    def string(self, offset):
        if offset < 0 or offset >= len(self.strings):
            raise ValueError("String offset is out of bounds")
        end = self.strings.find(b"\0", offset)
        if end == -1:
            raise ValueError("Unterminated symbol string")
        return self.strings[offset:end].decode("utf-8", errors="strict")

    def contains(self, address, size, executable=False):
        return any(s["address"] <= address and size <= (s["file_size"] if executable else s["memory_size"])
                   and address - s["address"] <= (s["file_size"] if executable else s["memory_size"]) - size
                   and (not executable or s["flags"] & 1) for s in self.segments)

    def id_table(self, tags):
        return {encode_id(value >> 48): self.string(value & 0xFFFFFFFF)
                for tag, value in self.dynamic if tag in tags}

    def inventory(self, names=None):
        names = names or {}
        libraries = self.id_table((0x61000013, 0x61000015))
        modules = self.id_table((0x6100000D, 0x6100000F))
        imports = []
        for index, symbol in enumerate(self.symbols):
            if symbol["section"] or not symbol["name"]:
                continue
            parts = symbol["name"].split("#")
            imports.append(dict(index=index, **symbol, friendly=names.get(parts[0], parts[0]),
                                library=libraries.get(parts[1], parts[1]) if len(parts) > 1 else "",
                                module=modules.get(parts[2], parts[2]) if len(parts) > 2 else ""))
        return dict(sha256=hashlib.sha256(self.raw).hexdigest(), entry=self.entry,
                    elf_type=self.elf_type, image_size=self.image_size, procparam=self.procparam,
                    segments=self.segments, imports=imports,
                    relocation_types=dict(Counter(r["type"] for r in self.relocations)),
                    relocation_count=len(self.relocations),
                    needed=[self.string(v) for t, v in self.dynamic if t == 1],
                    tls=[dict(address=p[3], file_size=p[5], memory_size=p[6], alignment=p[7])
                         for p in self.headers if p[0] == 7])

    def exports(self, names=None):
        names = names or {}
        libraries = self.id_table((0x61000013, 0x61000015))
        modules = self.id_table((0x6100000D, 0x6100000F))
        result = []
        seen = set()
        for symbol in self.symbols:
            if not symbol['section'] or symbol['binding'] not in (1, 2) or not symbol['name']:
                continue
            if symbol['section'] == 0xFFF1 or symbol['type'] not in (1, 2):
                raise ValueError('Unsupported exported symbol kind')
            if not self.contains(symbol['value'], max(1, symbol['size']), executable=symbol['type'] == 2):
                raise ValueError('Export is outside its mapped segment')
            parts = symbol['name'].split('#')
            if len(parts) != 3 or parts[1] not in libraries or parts[2] not in modules:
                raise ValueError('Export needs a resolved library and module identity')
            identity = (parts[0], libraries[parts[1]], modules[parts[2]])
            if identity in seen:
                raise ValueError('Duplicate export identity')
            seen.add(identity)
            result.append(dict(**symbol, friendly=names.get(parts[0], parts[0]),
                               library=identity[1], module=identity[2]))
        return result


def read_names(path):
    return {nid(name): name for name in Path(path).read_text(encoding="utf-8").splitlines() if name}
