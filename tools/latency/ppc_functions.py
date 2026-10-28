"""Query generated PPC instruction comments without changing guest bodies."""
import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def functions(game, module):
    for path in sorted((ROOT / 'generated' / game / module).glob('*_recomp.*.cpp')):
        text = path.read_text(encoding='utf-8')
        for m in re.finditer(r'^DEFINE_REX_FUNC\(sub_([0-9A-F]+)\) \{(.*?)^\}', text, re.S | re.M):
            entry = int(m[1], 16)
            pc = entry
            instructions = []
            for line in m[2].splitlines():
                label = re.fullmatch(r'loc_([0-9A-F]+):', line)
                # Codegen can omit unreachable instruction ranges. A label
                # restarts the address, not evidence that omitted bytes ran.
                if label:
                    pc = int(label[1], 16)
                if line.startswith('\t// '):
                    instructions.append((pc, line[4:].strip()))
                    pc += 4
            yield entry, instructions, path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('game')
    p.add_argument('module')
    p.add_argument('addresses', nargs='*', type=lambda s: int(s, 16))
    p.add_argument('--callers', action='store_true')
    p.add_argument('--contains', action='store_true', help='Functions containing the requested PCs')
    p.add_argument('--matching')
    p.add_argument('--start', type=lambda s: int(s, 16))
    p.add_argument('--end', type=lambda s: int(s, 16))
    a = p.parse_args()
    for entry, insns, path in functions(a.game, a.module):
        if a.contains:
            selected = insns if any(pc in a.addresses for pc, _ in insns) else []
        elif a.callers:
            selected = [(pc, asm) for pc, asm in insns
                        if any(asm == f'{op} 0x{target:x}' for target in a.addresses for op in ('bl', 'b'))]
        elif a.matching:
            if a.addresses and entry not in a.addresses:
                continue
            selected = [(pc, asm) for pc, asm in insns if re.search(a.matching, asm)]
        else:
            selected = insns if entry in a.addresses else []
        selected = [(pc, asm) for pc, asm in selected
                    if (a.start is None or pc >= a.start) and (a.end is None or pc < a.end)]
        if selected:
            print(f'{entry:08X} ({path.name}):')
            for pc, asm in selected:
                print(f'  {pc:08X} {asm}')


if __name__ == '__main__':
    main()
