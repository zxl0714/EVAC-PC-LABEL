"""Prepare ESPGaluda's reviewed input order without changing generated inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from ps2_translation_units import write_changed
from ps2_module_extras import table_entries


# The two pad routines remain consecutive and in their original order. The
# original SyncPath + SyncV routine runs before them, once per game iteration.
CALLS = ((0x1007b4, 0x11e840, 0x11d480),
         (0x1007bc, 0x1210a0, 0x11e840),
         (0x1007c4, 0x11d480, 0x1210a0))
FILES = ('FUN_001004d0_0x1004d0.cpp', 'entry_001007b0_0x1007b0.cpp')
POLICY = 'late-v1'


def source_files(generated):
    table = Path(generated) / 'register_functions.cpp'
    if not table.exists():
        return FILES  # Authored source-only rewrite fixtures.
    entries = table_entries(table)
    if not {0x1004d0, 0x1007b0} <= entries.keys():
        raise ValueError('Missing ESPGaluda frame-loop entries')
    return tuple(dict.fromkeys(entries[pc] + '.cpp' for pc in (0x1004d0, 0x1007b0)))


def rewrite(source):
    for pc, before, after in CALLS:
        # Retain each call's source PC, delay slot, return PC and resume label.
        # Only the direct target changes. Reject a different generated body.
        marker = f'    // 0x{pc:x}: 0x{0x0c000000 | (before >> 2):x}  jal         func_{before:X}\n'
        if source.count(marker) != 1:
            raise ValueError(f'Unexpected original frame call at {pc:#x}')
        start = source.index(marker)
        end = source.index('        return;\n    }', start)
        block = source[start:end]
        edge = f'ctx, 0x{before:X}u, 0x{pc:X}u, 0x{pc+8:X}u,'
        target = f'    ctx->pc = 0x{before:X}u;'
        if block.count(edge) != 1 or block.count(target) != 1:
            raise ValueError(f'Unexpected dispatch or continuation at {pc:#x}')
        block = block.replace(marker, f'    // Late-input call: {pc:#x}, original target {before:#x}.\n')
        block = block.replace(edge, f'ctx, 0x{after:X}u, 0x{pc:X}u, 0x{pc+8:X}u,')
        block = block.replace(target, f'    ctx->pc = 0x{after:X}u;')
        source = source[:start] + block + source[end:]
    return source


def prepare(generated, output):
    generated, output = Path(generated).resolve(), Path(output).resolve()
    if generated == output or generated in output.parents:
        raise ValueError('Candidate output must be outside the original generated tree')
    output.mkdir(parents=True, exist_ok=True)
    identities = []
    for name in source_files(generated):
        original = (generated/name).read_text(encoding='utf8')
        candidate = rewrite(original)
        write_changed(output/name, candidate)
        identities.append(dict(name=name,
            originalSha256=hashlib.sha256(original.encode()).hexdigest(),
            candidateSha256=hashlib.sha256(candidate.encode()).hexdigest()))
    write_changed(output/'identity.json', json.dumps(dict(format=2, policy=POLICY,
        scope='ESPGaluda SLPS-25352 input order',
        originalGenerated=str(generated), files=identities), indent=2)+'\n')


def verify(generated, output):
    generated, output = Path(generated).resolve(strict=True), Path(output).resolve(strict=True)
    identities = []
    for name in source_files(generated):
        original = (generated/name).read_text(encoding='utf8')
        expected = rewrite(original)
        if (output/name).read_text(encoding='utf8') != expected:
            raise ValueError('ESPGaluda input-order body differs: '+name)
        identities.append(dict(name=name,
            originalSha256=hashlib.sha256(original.encode()).hexdigest(),
            candidateSha256=hashlib.sha256(expected.encode()).hexdigest()))
    expected = dict(format=2, policy=POLICY, scope='ESPGaluda SLPS-25352 input order',
                    originalGenerated=str(generated), files=identities)
    if json.loads((output/'identity.json').read_text(encoding='utf8')) != expected:
        raise ValueError('ESPGaluda input-order identity differs')
    return expected


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generated', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.generated, args.output)
