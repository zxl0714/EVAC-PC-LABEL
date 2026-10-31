"""Batch private generated C++ without adding each guest label to CMake's graph."""
import argparse
import json
from pathlib import Path
import re


def write_changed(path, text):
    if path.exists() and path.read_text(encoding='utf-8') == text:
        return
    path.write_text(text, encoding='utf-8', newline='\n')


def make_units(generated, output, batch_size=256, symbols=()):
    generated, output = Path(generated).resolve(), Path(output).resolve()
    if not 1 <= batch_size <= 1024 or generated == output:
        raise ValueError('Invalid translation unit output or batch size')
    for name in ('register_functions.cpp', 'ps2_recompiled_functions.h'):
        if not (generated/name).is_file():
            raise ValueError('Missing generated function inventory')
    sources = sorted(path for path in generated.iterdir() if path.suffix == '.cpp')
    if any(not path.is_file() or path.is_symlink() for path in sources):
        raise ValueError('Expected regular generated C++ files')
    output.mkdir(parents=True, exist_ok=True)
    units = []
    symbol_users = {symbol: [] for symbol in symbols}
    symbol_pattern = re.compile(r'\b(?:'+ '|'.join(re.escape(s) for s in symbols)+r')\b') if symbols else None
    for index, start in enumerate(range(0, len(sources), batch_size)):
        unit = output/f'batch_{index:04}.cpp'
        text = '// Generated PS2 compilation batch. Original bodies remain unchanged.\n'
        text += ''.join(f'#include "{path.as_posix()}"\n' for path in sources[start:start+batch_size])
        write_changed(unit, text)
        units.append(unit)
        if symbol_pattern:
            referenced = set()
            for source in sources[start:start+batch_size]:
                referenced.update(symbol_pattern.findall(source.read_text(encoding='utf-8')))
            for symbol in referenced:
                symbol_users[symbol].append(str(unit))
    manifest = 'set(PS2_TRANSLATION_UNITS\n'
    manifest += ''.join('  "'+path.as_posix().replace(';', '\\;')+'"\n' for path in units)+')\n'
    write_changed(output/'sources.cmake', manifest)
    return dict(sourceFiles=len(sources), translationUnits=len(units),
                manifest=str(output/'sources.cmake'), symbolUsers=symbol_users)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('generated', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--batch-size', type=int, default=256)
    args = parser.parse_args()
    print(json.dumps(make_units(args.generated, args.output, args.batch_size)))
