"""Keep generated PS2 dispatch arrays in BSS and populate them from compact runs."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from ps2_translation_units import write_changed


def view_path(source, root):
    key = hashlib.sha256(Path(source).resolve().as_posix().encode()).hexdigest()[:16]
    return Path(root) / key / 'register_functions.cpp'


def render(text):
    marker = 'namespace {\nstruct GeneratedFunctionTableInitializer {\n'
    prefix, body = text.split(marker, 1)
    match = re.fullmatch(r'    GeneratedFunctionTableInitializer\(\) \{\n(.*?)'
                         r'    \}\n\};\nstatic const GeneratedFunctionTableInitializer '
                         r'g_generatedFunctionTableInitializer;\n\}\s*', body, re.S)
    if match is None:
        raise ValueError('Unrecognized PS2 function-table initializer')
    values = {}
    for suffix in ('Base', 'End', 'SlotCount'):
        found = re.search(r'extern const uint32_t g_ps2RecompiledFunctionTable' + suffix +
                          r' = (0x[0-9a-fA-F]+|[0-9]+)u;', prefix)
        if found is None:
            raise ValueError('Missing PS2 function-table extent')
        values[suffix] = int(found[1], 0)
    base, end, slots = (values[k] for k in ('Base', 'End', 'SlotCount'))
    if ((base | end) & 3 or end < base or end - base != slots * 4 or
            not re.search(r'g_ps2RecompiledFunctionTable\[' + str(max(1, slots)) + r'u\] = \{\};', prefix)):
        raise ValueError('Invalid PS2 function-table extent')
    runs, entries, previous = [], 0, -1
    for line in match[1].splitlines():
        item = re.fullmatch(r'\s*g_ps2RecompiledFunctionTable\[([0-9]+)\] = '
                            r'([A-Za-z_][A-Za-z_0-9]*); // 0x([0-9a-fA-F]+)', line)
        if item is None:
            raise ValueError('Unrecognized PS2 function-table entry')
        slot, name, pc = int(item[1]), item[2], int(item[3], 16)
        if slot <= previous or slot >= slots or pc != base + slot * 4:
            raise ValueError('Invalid, duplicate or unordered PS2 function-table entry')
        if runs and slot == runs[-1][0] + runs[-1][1] and name == runs[-1][2]:
            runs[-1][1] += 1
        else:
            runs.append([slot, 1, name])
        previous = slot
        entries += 1
    output = prefix + '#include "function_table.h"\nnamespace {\n'
    if runs:
        output += 'const cave::ps2::FunctionTableRun kFunctionRuns[]{\n'
        output += ''.join(f'    {{{first}u,{count}u,{name}}},\n' for first, count, name in runs)
        output += '};\n'
    output += ('struct GeneratedFunctionTableInitializer {\n'
               '    GeneratedFunctionTableInitializer() {\n'
               '        cave::ps2::PopulateFunctionTable(g_ps2RecompiledFunctionTable, '
               'g_ps2RecompiledFunctionTableSlotCount, ' + ('kFunctionRuns' if runs else '{}') + ');\n'
               '    }\n};\nstatic const GeneratedFunctionTableInitializer '
               'g_generatedFunctionTableInitializer;\n}\n')
    return output, dict(format=1, slots=slots, entries=entries, runs=len(runs),
                         denseBytes=slots * 8, runBytes=len(runs) * 16)


def prepare(source, root):
    source = Path(source).resolve()
    output = view_path(source, root)
    text, report = render(source.read_text(encoding='utf8'))
    output.parent.mkdir(parents=True, exist_ok=True)
    write_changed(output, text)
    report.update(source=str(source), output=str(output),
                  sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  outputSha256=hashlib.sha256(text.encode()).hexdigest())
    write_changed(output.with_suffix('.json'), json.dumps(report, indent=2) + '\n')
    return output


def verify(source, root):
    expected, report = render(Path(source).read_text(encoding='utf8'))
    output = view_path(source, root)
    if output.read_text(encoding='utf8') != expected:
        raise ValueError('Compiled PS2 function-table view differs from its original mapping')
    report.update(source=str(Path(source).resolve()), outputSha256=hashlib.sha256(expected.encode()).hexdigest())
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(prepare(args.source, args.output).as_posix())
