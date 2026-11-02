"""Share PS2 internal entries with their decoded owner before code generation."""
import argparse
import bisect
import csv
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import tomllib

POLICY = 'owner-entries-v1'


def load_words(elf):
    data = Path(elf).read_bytes()
    if data[:6] != b'\x7fELF\x01\x01' or len(data) < 52:
        raise ValueError('Compaction requires little-endian ELF32')
    offset = struct.unpack_from('<I', data, 28)[0]
    size, count = struct.unpack_from('<HH', data, 42)
    if size < 32 or offset + size * count > len(data):
        raise ValueError('Invalid ELF program headers')
    segments = []
    for index in range(count):
        kind, file_offset, address, _, length, _, _, _ = struct.unpack_from('<8I', data, offset + index * size)
        if kind == 1:
            if file_offset + length > len(data):
                raise ValueError('Truncated ELF segment')
            segments.append((address, address + length, file_offset))
    def word(pc):
        for start, end, file_offset in segments:
            if start <= pc and pc + 4 <= end:
                return struct.unpack_from('<I', data, file_offset + pc - start)[0]
        return None
    return word


def has_delay_slot(opcode):
    if opcode is None:
        return False
    primary = opcode >> 26
    return (primary in (1, 2, 3, 4, 5, 6, 7, 20, 21, 22, 23) or
            (primary == 0 and opcode & 63 in (8, 9)) or
            (primary in (16, 17, 18) and (opcode >> 21) & 31 == 8))


def protected_starts(general):
    result = set()
    for key in ('stubs', 'skip', 'compact_keep_entries'):
        for selector in general.get(key, []):
            address = selector.rsplit('@', 1)[-1]
            if re.fullmatch(r'0[xX][0-9a-fA-F]+|[0-9]+', address):
                result.add(int(address, 0))
    return result


def analyze(rows, word, protected=()):
    rows = [dict(row) for row in rows]
    starts = set()
    for row in rows:
        start, end = int(row['Start'], 0), int(row['End'], 0)
        if (start in starts or (start | end) & 3 or end <= start or
                int(row['Size'], 0) < 0):
            raise ValueError('Invalid or duplicate PS2 code interval')
        starts.add(start)
    rows.sort(key=lambda row: int(row['Start'], 0))
    owners = [row for row in rows if not row['Name'].startswith('entry_')]
    owner_starts = [int(row['Start'], 0) for row in owners]
    protected = set(protected)
    retained, aliases, excluded = [], [], {}
    for row in rows:
        start, end = int(row['Start'], 0), int(row['End'], 0)
        reason = None
        if not row['Name'].startswith('entry_'):
            retained.append(row)
            continue
        index = bisect.bisect_left(owner_starts, start) - 1
        owner = owners[index] if index >= 0 else None
        if start in protected:
            reason = 'protected'
        elif owner is None or end > int(owner['End'], 0):
            reason = 'standalone-or-crossing'
        elif int(owner['Start'], 0) in protected:
            reason = 'native-owner'
        elif has_delay_slot(word(start - 4)):
            # The upstream owner emitter places a delay-slot label before its
            # branch. Keep this independent body until that case is supported.
            reason = 'delay-slot-entry'
        elif word(start) is None or word(end - 4) is None:
            reason = 'unmapped'
        if reason:
            retained.append(row)
            excluded[reason] = excluded.get(reason, 0) + 1
        else:
            aliases.append(dict(entry=start, end=end, owner=int(owner['Start'], 0), name=row['Name']))
    return retained, dict(format=1, policy=POLICY, inputFunctions=len(rows),
        outputFunctions=len(retained), sharedEntries=len(aliases),
        inputInstructionBytes=sum(int(row['End'], 0)-int(row['Start'], 0) for row in rows),
        outputInstructionBytes=sum(int(row['End'], 0)-int(row['Start'], 0) for row in retained),
        excluded=excluded, aliases=aliases)


def prepare(config, output):
    config, output = Path(config).resolve(), Path(output).resolve()
    general = tomllib.loads(config.read_text(encoding='utf8'))['general']
    function_map, elf = Path(general['ghidra_output']), Path(general['input'])
    original_map = function_map.read_bytes()
    rows = list(csv.DictReader(io.StringIO(original_map.decode('utf8'))))
    retained, report = analyze(rows, load_words(elf), protected_starts(general))
    if output.exists():
        raise FileExistsError('Use a new compaction output directory')
    output.mkdir(parents=True)
    compact_map = output / 'functions.csv'
    stream = io.StringIO(newline='')
    writer = csv.DictWriter(stream, fieldnames=['Name', 'Start', 'End', 'Size'], lineterminator='\n')
    writer.writeheader()
    writer.writerows(retained)
    compact_map.write_text(stream.getvalue(), encoding='utf8', newline='\n')
    general = dict(general)
    general.pop('compact_entries', None)
    general.pop('compact_keep_entries', None)
    general['ghidra_output'] = compact_map.as_posix()
    general['entry_points'] = sorted(set(general.get('entry_points', [])) |
                                     {f'0x{row["entry"]:08x}' for row in report['aliases']})
    compact_config = output / 'config.toml'
    compact_config.write_text('[general]\n' + ''.join(
        key + ' = ' + json.dumps(value, ensure_ascii=True) + '\n' for key, value in general.items()),
        encoding='utf8', newline='\n')
    report.update(config=str(compact_config), originalConfigSha256=hashlib.sha256(config.read_bytes()).hexdigest(),
        inputMapSha256=hashlib.sha256(original_map).hexdigest(),
        outputMapSha256=hashlib.sha256(compact_map.read_bytes()).hexdigest(),
        elfSha256=hashlib.sha256(elf.read_bytes()).hexdigest())
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8', newline='\n')
    return report


def verify_generated(report, generated, entries):
    """Check actual emitter owners, including authoritative ELF symbol bounds."""
    groups = {}
    for item in report['aliases']:
        symbol = entries.get(item['entry'])
        if symbol is None:
            raise ValueError(f'Compaction lost callable PC {item["entry"]:#x}')
        groups.setdefault(symbol, []).append(item)
    remapped = 0
    for symbol, items in groups.items():
        source = (Path(generated)/(symbol+'.cpp')).read_text(encoding='utf8')
        bounds = re.search(r'// Address: 0x([0-9a-f]+) - 0x([0-9a-f]+)', source)
        if bounds is None:
            raise ValueError('Compacted entry needs a generated guest body')
        start, end = (int(value, 16) for value in bounds.groups())
        labels = {int(value,16) for value in re.findall(r'case 0x([0-9a-f]+)u: goto label_',source)}
        for item in items:
            pc = item['entry']
            if not start <= pc < end or (pc != start and pc not in labels):
                raise ValueError(f'Compacted body cannot resume at {pc:#x}')
            if entries.get(item['owner']) != symbol:
                remapped += 1
    return dict(verifiedEntries=sum(map(len, groups.values())), actualOwners=len(groups),
                elfOwnerRemappings=remapped)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = prepare(args.config, args.output)
    print(json.dumps({k: v for k, v in result.items() if k != 'aliases'}, indent=2))
