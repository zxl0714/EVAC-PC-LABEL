"""Review unregistered, data-referenced PPC forwarding callbacks without execution."""
import argparse
import json
from pathlib import Path
import re
import struct

from project import ROOT, digest, write_json_new
from analysis_paths import analysis_child, private_output


def forwarding_size(code, offset):
    """Conservative straight-line thunk, at most eight instructions, no frame."""
    if offset < 0 or offset % 4 or offset >= len(code):
        return None
    count = min(8, (len(code) - offset) // 4)
    # A standalone forwarding entry may use argument/volatile registers, not
    # a caller's saved frame. Exception tables also contain aligned code
    # pointers; their r29/r30 <- [r31] tails are not callable entrypoints.
    volatile = {0, *range(3, 13)}
    readable = volatile | {2}  # r2 is the ABI's read-only TOC value.
    for index in range(count):
        word = struct.unpack_from('>I', code, offset + index * 4)[0]
        opcode, rt, ra = word >> 26, (word >> 21) & 31, (word >> 16) & 31
        if word == 0x4e800420:  # bctr, following an explicit mtctr
            if index and (previous & 0xfc1fffff) == 0x7c0903a6:
                return (index + 1) * 4
            return None
        if opcode == 18 and not word & 3:  # relative b, not bl/ba
            return (index + 1) * 4
        if opcode in (14, 15, 32, 40, 42):
            if rt not in volatile or ra not in readable:
                return None
        elif opcode == 24:  # ori rA,rS,imm
            if ra not in volatile or rt not in readable:
                return None
        elif opcode == 31:
            xo = (word >> 1) & 1023
            if xo == 444:  # mr is or rA,rS,rS; reject linker/SEH self-nops.
                rb = (word >> 11) & 31
                if rt != rb or rt == ra or ra not in volatile or rt not in readable:
                    return None
            elif xo == 467:
                if (word & 0xfc1fffff) != 0x7c0903a6 or rt not in readable:  # mtctr only
                    return None
            else:
                return None
        else:
            return None
        previous = word
    return None


def pointer_references(data, address, ranges, known):
    result = {}
    for offset in range(0, len(data) - 3, 4):
        target = struct.unpack_from('>I', data, offset)[0]
        if target % 4 == 0 and target not in known and any(start <= target < end for start, end in ranges):
            result.setdefault(target, []).append(address + offset)
    return result


def code_pointer_runs(data, address, ranges, known):
    """Metadata for review, not a proof of a vtable (EH tables can look alike)."""
    result, run = [], []
    count = len(data) // 4
    for index in range(count + 1):
        target = struct.unpack_from('>I', data, index * 4)[0] if index < count else None
        if target is not None and target % 4 == 0 and any(start <= target < end for start, end in ranges):
            run.append(target)
            continue
        unknown = sorted(set(run) - known)
        if len(run) >= 3 and unknown and any(item in known for item in run):
            result.append(dict(address=f'0x{address + (index - len(run)) * 4:08X}', entries=len(run),
                unregisteredTargets=[f'0x{item:08X}' for item in unknown]))
        run = []
    return result


def readonly_leaf_size(code, offset, max_bytes=512):
    """Bounded CFG candidate: no linked calls, frame, stores or saved registers.

    This is a review aid, not a proof that a data pointer is a function. In
    particular it does not classify exception tables or install declarations.
    """
    if offset < 0 or offset % 4 or offset >= len(code):
        return None
    limit = min(len(code), offset + max_bytes)
    pending, seen, positions, returns = [(offset, False)], set(), set(), 0
    volatile = {0, *range(3, 13)}
    readable = volatile | {2}
    while pending:
        position, ctr_ready = pending.pop()
        state = (position, ctr_ready)
        if state in seen:
            continue
        if position < offset or position % 4 or position + 4 > limit:
            return None
        seen.add(state)
        positions.add(position)
        word = struct.unpack_from('>I', code, position)[0]
        op, rt, ra, rb = word >> 26, (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31
        if word == 0x4e800020:  # blr
            returns += 1
            continue
        if word == 0x4e800420:  # Tail dispatch; every incoming path must set CTR.
            if not ctr_ready:
                return None
            returns += 1
            continue
        if op in (16, 18):
            if word & 3:  # no linked or absolute branches
                return None
            bits = 16 if op == 16 else 26
            displacement = word & ((1 << bits) - 4)
            if displacement & (1 << (bits - 1)):
                displacement -= 1 << bits
            pending.append((position + displacement, ctr_ready))
            if op == 16:
                pending.append((position + 4, ctr_ready))
            continue
        if op in (14, 15, 32, 34, 40, 42):
            valid = rt in volatile and ra in readable
        elif op in (10, 11):  # unsigned/signed immediate compare
            valid = ra in readable and ((word >> 23) & 7) in {0, 1, 5, 6, 7}
        elif op in (20, 21, 24, 25, 26, 27, 28, 29):
            valid = ra in volatile and rt in readable
        elif op == 31:
            xo = (word >> 1) & 1023
            if xo == 467 and (word & 0xfc1fffff) == 0x7c0903a6:  # mtctr
                valid = rt in readable
                ctr_ready = True
            elif xo in (0, 32):  # integer compare, volatile CR fields only
                valid = ra in readable and rb in readable and ((word >> 23) & 7) in {0, 1, 5, 6, 7}
            elif xo in (8, 10, 40, 136, 138, 235, 266):
                valid = rt in volatile and ra in readable and rb in readable
            elif xo in (28, 60, 124, 316, 444, 476):
                valid = ra in volatile and rt in readable and rb in readable
            else:
                valid = False
        else:
            valid = False
        if not valid:
            return None
        pending.append((position + 4, ctr_ready))
    return max(positions) + 4 - offset if returns else None


def inventory(analysis, data_dir, game='espgaluda2'):
    if game not in ('espgaluda2', 'sdoj'):
        raise ValueError('Unsupported game adaptation')
    analysis = Path(analysis).resolve()
    data_dir = Path(data_dir).resolve()
    receipt = json.loads(analysis_child(analysis, 'analysis.json').read_text(encoding='utf-8'))
    if receipt.get('guestExecution') is not False or receipt.get('game') != game:
        raise ValueError('Expected a static census for the selected game')
    expected = {item['path']: item['sha256'] for item in json.loads(
        (ROOT / 'games' / game / 'inputs.json').read_text(encoding='utf-8'))['xex']}
    modules = []
    seen = set()
    for item in receipt['modules']:
        module = item['module']
        if module not in expected or module in seen:
            raise ValueError('Unknown or duplicate census module')
        seen.add(module)
        name = Path(module).stem
        # Validate both the original input and each dumped section before using it.
        if item['inputSha256'] != expected[module] or digest(analysis_child(data_dir, module)) != expected[module]:
            raise ValueError('Local XEX differs from the supported version')
        census = json.loads(analysis_child(analysis, name + '/census.json').read_text(encoding='utf-8'))
        if census.get('module') != module or census.get('guestExecution') is not False:
            raise ValueError('Unexpected census module or execution state')
        hashes = {dump['path']: dump['sha256'] for dump in item['dumps']}
        sections = []
        for section in census['sections']:
            if not section['inImage']:
                continue
            relative = name + '/' + section['dump']
            path = analysis_child(analysis, relative)
            if path.stat().st_size != section['bytes'] or digest(path) != hashes[relative]:
                raise ValueError('Census section hash mismatch')
            sections.append((section, path.read_bytes()))
        registers = list((ROOT / 'generated' / game / name).glob('*_register.cpp'))
        if len(registers) != 1:
            raise ValueError('Generate this module before reviewing callback coverage')
        known = {int(value, 16) for value in re.findall(
            r'SetFunction\(0x([0-9A-F]+)', registers[0].read_text(encoding='utf-8'))}
        code = [(section, data) for section, data in sections if section['executable']]
        ranges = [(section['address'], section['address'] + len(data)) for section, data in code]
        references, pointer_runs = {}, []
        for section, data in sections:
            if section['name'] not in ('.rdata', '.data'):
                continue
            pointer_runs.extend(code_pointer_runs(data, section['address'], ranges, known))
            for target, sources in pointer_references(data, section['address'], ranges, known).items():
                references.setdefault(target, []).extend(sources)
        candidates, leaves = [], []
        for target, sources in sorted(references.items()):
            section, data = next((section, data) for section, data in code
                if section['address'] <= target < section['address'] + len(data))
            size = forwarding_size(data, target - section['address'])
            if size:
                candidates.append(dict(address=f'0x{target:08X}', bytes=size,
                    references=[f'0x{address:08X}' for address in sources]))
            else:
                size = readonly_leaf_size(data, target - section['address'])
                if size:
                    leaves.append(dict(address=f'0x{target:08X}', bytes=size,
                        references=[f'0x{address:08X}' for address in sources]))
        modules.append(dict(module=item['module'], missingDataTargets=len(references),
            forwardingCandidates=candidates, readonlyLeafCandidates=leaves, codePointerRuns=pointer_runs))
    return dict(format=1, game=game, guestExecution=False, automaticEdits=False, modules=modules)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('analysis', type=Path)
    parser.add_argument('--game', choices=('espgaluda2', 'sdoj'), default='espgaluda2')
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = private_output(args.output, args.analysis, args.data_dir)
    result = inventory(args.analysis, args.data_dir, args.game)
    write_json_new(output, result)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
