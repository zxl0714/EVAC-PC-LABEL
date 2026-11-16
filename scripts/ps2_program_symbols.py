"""Scope the native symbols of separately linked PS2 disc executables."""

import argparse
from pathlib import Path
import re

from ps2_translation_units import write_changed


def prepare(generated, prefix, output):
    if not re.fullmatch(r'[a-z][a-z0-9_]*', prefix):
        raise ValueError('Invalid PS2 program symbol prefix')
    symbols = set()
    for header in ('ps2_recompiled_functions.h', 'ps2_recompiled_stubs.h'):
        for line in (generated / header).read_text(encoding='utf-8').splitlines():
            if line.startswith('void '):
                symbols.add(re.match(r'void ([A-Za-z_][A-Za-z_0-9]*)\(', line)[1])
    if not symbols:
        raise ValueError('No native PS2 program symbols found')
    aliases = {name: f'{prefix}_{name}' for name in symbols}
    stem = 'g_ps2RecompiledFunctionTable'
    aliases.update({stem + suffix: f'{prefix}_table{suffix}'
                    for suffix in ('', 'Base', 'End', 'SlotCount')})
    write_changed(output, '#pragma once\n' + ''.join(
        f'#define {name} {alias}\n' for name, alias in sorted(aliases.items())))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('generated', type=Path)
    parser.add_argument('prefix')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    prepare(args.generated, args.prefix, args.output)
