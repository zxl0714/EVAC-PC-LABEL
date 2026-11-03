"""Batch private generated C++ without adding each guest label to CMake's graph."""
import argparse
import json
import os
from pathlib import Path
import re


def write_changed(path, text):
    if path.exists() and path.read_text(encoding='utf-8') == text:
        return
    path.write_text(text, encoding='utf-8', newline='\n')


def make_units(generated, output, batch_size=256, symbols=(), table_root=None):
    generated, output = Path(generated).resolve(), Path(output).resolve()
    if not 1 <= batch_size <= 1024 or generated == output:
        raise ValueError('Invalid translation unit output or batch size')
    for name in ('register_functions.cpp', 'ps2_recompiled_functions.h'):
        if not (generated/name).is_file():
            raise ValueError('Missing generated function inventory')
    facts={}
    with os.scandir(generated) as directory:
        for entry in directory:
            if not entry.name.endswith('.cpp'):continue
            if entry.is_symlink() or not entry.is_file(follow_symlinks=False):
                raise ValueError('Expected regular generated C++ files')
            stat=entry.stat(follow_symlinks=False)
            facts[entry.name]=[entry.name,stat.st_size,stat.st_mtime_ns]
    sources = sorted(generated/name for name in facts)
    table_view = None
    if table_root is not None:
        from ps2_function_table import prepare
        table_view = prepare(generated/'register_functions.cpp', table_root)
    output.mkdir(parents=True, exist_ok=True)
    units = []
    symbol_users = {symbol: [] for symbol in symbols}
    symbol_pattern = re.compile(r'\b(?:'+ '|'.join(re.escape(s) for s in symbols)+r')\b') if symbols else None
    cache_path=output/'references.json'
    cache={}
    if symbols and cache_path.is_file():
        try:
            prior=json.loads(cache_path.read_text(encoding='utf-8'))
            if (prior.get('format')==1 and prior.get('generated')==str(generated)
                    and prior.get('symbols')==list(symbols) and isinstance(prior.get('batches'),dict)):
                cache=prior.get('batches',{})
        except (ValueError,OSError,AttributeError,TypeError):pass
    batches={}
    for index, start in enumerate(range(0, len(sources), batch_size)):
        unit = output/f'batch_{index:04}.cpp'
        text = '// Generated PS2 compilation batch. Original bodies remain unchanged.\n'
        text += ''.join(f'#include "{(table_view if table_view is not None and path.name == "register_functions.cpp" else path).as_posix()}"\n'
                        for path in sources[start:start+batch_size])
        write_changed(unit, text)
        units.append(unit)
        if symbol_pattern:
            inputs=[facts[source.name] for source in sources[start:start+batch_size]]
            prior=cache.get(unit.name,{})
            if (isinstance(prior,dict) and prior.get('inputs')==inputs
                    and isinstance(prior.get('references'),list)
                    and all(isinstance(name,str) for name in prior['references'])
                    and set(prior['references'])<=set(symbols)):
                referenced=set(prior['references'])
            else:
                referenced = set()
                for source in sources[start:start+batch_size]:
                    referenced.update(symbol_pattern.findall(source.read_text(encoding='utf-8')))
            batches[unit.name]=dict(inputs=inputs,references=sorted(referenced))
            for symbol in referenced:
                symbol_users[symbol].append(str(unit))
    if symbols:
        write_changed(cache_path,json.dumps(dict(format=1,generated=str(generated),symbols=list(symbols),batches=batches),sort_keys=True))
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
