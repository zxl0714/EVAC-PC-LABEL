"""Prepare ESPGaluda's reviewed analysis inputs and generate native game code."""
import argparse
import csv
import gzip
import hashlib
import io
import json
import re
from pathlib import Path
import struct
import sys

from project import ROOT, digest, reject_link
from ps2_codegen import generate, tree_identity, equivalence_path, verify_equivalence, refresh_tool
from ps2_disc import Iso9660, inspect as inspect_disc
from ps2_module import analysis_elf, prepare_modules
from ps2_translation_units import write_changed


METADATA = ROOT / 'games/espgaluda/analysis/manifest.json'


def default_output():
    paths = [METADATA, Path(__file__), ROOT / 'scripts/ps2_module.py',
             ROOT / 'games/espgaluda/modules.json', ROOT / 'games/espgaluda/sdk_bindings.cpp',
             ROOT / 'out/ps2-tools/bin/ps2_recomp.exe']
    fingerprint = hashlib.sha256(''.join(digest(path) for path in paths).encode()).hexdigest()[:20]
    return ROOT / '.local/ps2/espgaluda' / fingerprint


def find_iso(directory):
    directory = Path(directory).resolve()
    if directory.is_file():
        candidates = [directory]
    else:
        children = list(directory.iterdir())
        candidates = [p for p in children if p.suffix.lower() == '.iso']
        if len(children) > 4096 or len(candidates) > 64:
            raise ValueError('Choose a smaller directory containing the game ISO')
    matches = []
    expected = json.loads(METADATA.read_text(encoding='utf-8'))['bootSha256']
    for path in candidates:
        reject_link(path)
        try:
            result = inspect_disc(path)
        except (ValueError, OSError):
            continue
        if result['candidateGame'] == 'espgaluda' and result['elf']['sha256'] == expected:
            matches.append(path)
    if len(matches) != 1:
        raise ValueError('Choose a directory containing one supported ESPGaluda ISO')
    return matches[0]


def read_map(directory, spec):
    name = spec['map']
    if Path(name).name != name or not name.endswith('.csv.gz'):
        raise ValueError('Invalid analysis map path')
    path = directory / name
    reject_link(path)
    with gzip.open(path, 'rb') as stream:
        data = stream.read(64 * 1024 * 1024 + 1)
    if len(data) > 64 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != spec['mapSha256']:
        raise ValueError('Analysis map checksum differs: ' + name)
    reader = csv.DictReader(io.StringIO(data.decode('utf-8')))
    if reader.fieldnames != ['Name', 'Start', 'End', 'Size']:
        raise ValueError('Invalid analysis map columns')
    rows = 0
    for row in reader:
        if (set(row) != set(reader.fieldnames) or
                not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]{0,255}', row['Name']) or
                not 0 <= int(row['Start'], 0) <= int(row['End'], 0) < 0x2000000 or
                not 0 <= int(row['Size'], 0) < 0x2000000):
            raise ValueError('Analysis address is outside EE memory')
        rows += 1
    if rows != spec['rows'] or not rows:
        raise ValueError('Analysis map inventory differs')
    return data


def load_metadata(path=METADATA):
    path = Path(path)
    spec = json.loads(path.read_text(encoding='utf-8'))
    if (spec.get('format') != 1 or spec.get('game') != 'espgaluda' or
            [m.get('id') for m in spec.get('modules', [])] != list(range(18))):
        raise ValueError('Expected the complete ESPGaluda module inventory')
    for module in spec['modules']:
        read_map(path.parent, module)
        if 'extra' in module:
            read_map(path.parent, module['extra'])
        for config in (module, module.get('extra', {})):
            if any(k in config.get('general', {}) for k in ('input', 'output', 'ghidra_output', 'sdk_bindings')):
                raise ValueError('Analysis metadata must not contain machine paths')
    return spec


def analysis_sources(root=ROOT):
    metadata = Path(root) / 'games/espgaluda/analysis/manifest.json'
    if not metadata.exists():
        return {}
    spec = load_metadata(metadata)
    result = {}
    for module in spec['modules']:
        for item in [module, *([module['extra']] if 'extra' in module else [])]:
            path = metadata.parent / item['map']
            result[path.relative_to(root).as_posix()] = read_map(metadata.parent, item)
    return result


def config_text(general, elf, output, function_map, sdk=None):
    values = dict(general, input=elf.as_posix(), output=output.as_posix(), ghidra_output=function_map.as_posix())
    if sdk is not None:
        values['sdk_bindings'] = sdk.as_posix()
    # These reviewed options contain only strings, integers, booleans and lists.
    return '[general]\n' + ''.join(k + ' = ' + json.dumps(v, ensure_ascii=True) + '\n' for k, v in values.items())


def prepare(iso, output, metadata=METADATA):
    iso, output, metadata = Path(iso).resolve(), Path(output).resolve(), Path(metadata).resolve()
    reject_link(iso)
    reject_link(output)
    output.relative_to(ROOT.resolve())
    if output.relative_to(ROOT.resolve()).parts[0] not in ('.local', 'generated', 'out'):
        raise ValueError('Use a project build directory for generated game inputs')
    spec = load_metadata(metadata)
    if output.exists():
        raise FileExistsError('Choose a fresh preparation directory; existing inputs are preserved')
    declarations = ROOT / 'games/espgaluda/modules.json'
    prepared = output / 'disc'
    receipt = prepare_modules(iso, declarations, prepared)
    declared = {m['id']: m for m in json.loads(declarations.read_text(encoding='utf-8'))['modules']}
    with iso.open('rb') as stream:
        disc = Iso9660(stream, iso.stat().st_size)
        entries = disc.entries()
        for module in spec['modules']:
            number = module['id']
            directory = output / f'module-{number:02}'
            directory.mkdir()
            elf = prepared / 'boot.elf' if not number else prepared / f'module-{number:02}/analysis.elf'
            if number:
                raw = disc.contents(entries[declared[number]['discPath']])
                image = analysis_elf(raw, int(module['entry'], 0),
                                     [int(p, 0) for p in module['roots']], root_prefix=module['rootPrefix'])
                if hashlib.sha256(image).hexdigest() != module['elfSha256']:
                    raise ValueError('Reconstructed analysis ELF differs: ' + str(number))
                elf.write_bytes(image)
                original = next(m for m in receipt['modules'] if m['module_id'] == number)
                original.update(analysisSha256=module['elfSha256'], roots=[int(p, 0) for p in module['roots']])
            if digest(elf) != module['elfSha256']:
                raise ValueError('Analysis program checksum differs')
            for label, config in [('base', module), *([('extra', module['extra'])] if 'extra' in module else [])]:
                function_map = directory / (label + '.csv')
                function_map.write_bytes(read_map(metadata.parent, config))
                text = config_text(config['general'], elf, directory / (label + '-generated'), function_map,
                                   ROOT / 'games/espgaluda/sdk_bindings.cpp' if not number and label == 'base' else None)
                (directory / (label + '.toml')).write_text(text, encoding='utf-8', newline='\n')
    (prepared / 'prepared.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8', newline='\n')
    plan = dict(format=1, metadataSha256=digest(metadata), metadata=str(metadata), iso=str(iso),
                output=str(output), modules=spec['modules'])
    (output / 'plan.json').write_text(json.dumps(plan, indent=2) + '\n', encoding='utf-8', newline='\n')
    return plan


def generate_all(output, progress=None, refresh=False, source_identities=None):
    output = Path(output).resolve()
    plan = json.loads((output / 'plan.json').read_text(encoding='utf-8'))
    metadata = Path(plan['metadata'])
    reviewed = load_metadata(metadata)
    if (plan.get('format') != 1 or plan['metadataSha256'] != digest(metadata) or
            plan['output'] != str(output) or plan['modules'] != reviewed['modules']):
        raise ValueError('Preparation plan differs; prepare a fresh build directory')
    stages, extras = [], []
    for module in plan['modules']:
        number = module['id']
        directory = output / f'module-{number:02}'
        for label in ['base', *(['extra'] if 'extra' in module else [])]:
            config, report = directory / (label + '.toml'), directory / (label + '-codegen.json')
            if progress:
                progress(f'[ps2-codegen] module={number:02} kind={label} started')
            if report.exists():
                import tomllib
                previous = json.loads(report.read_text(encoding='utf-8'))
                general = tomllib.loads(config.read_text(encoding='utf-8'))['general']
                expected = dict(configSha256=digest(config), toolSha256=digest(ROOT / 'out/ps2-tools/bin/ps2_recomp.exe'),
                                elfSha256=digest(Path(general['input'])), functionMapSha256=digest(Path(general['ghidra_output'])))
                if 'sdk_bindings' in general:
                    expected['sdkBindingsSha256']=digest(Path(general['sdk_bindings']))
                if any(previous.get(k) != v for k, v in expected.items() if k != 'toolSha256'):
                    raise ValueError('Existing generation inputs differ; prepare a fresh build directory')
                if any(previous.get('summary',{}).get(k) != 0 for k in
                       ('errors','decodeFailures','unhandledInstructions','criticalGuestFallbacks')):
                    raise ValueError('Existing generation has incomplete translation')
                if not (directory / (label + '-generated/register_functions.cpp')).is_file():
                    raise ValueError('Existing generation is incomplete')
                if previous.get('toolSha256') != expected['toolSha256']:
                    if refresh and not equivalence_path(report, expected['toolSha256']).exists():
                        identity = refresh_tool(config, report)
                    else:
                        identity = verify_equivalence(config, report, expected['toolSha256'])
                    if source_identities is not None:
                        source_identities[str(directory / (label + '-generated'))] = identity
            else:
                generate(config, directory / (label + '-codegen.log'), report)
            if source_identities is not None:
                path = str(directory / (label + '-generated'))
                if path not in source_identities:
                    source_identities[path] = dict(tree_identity(Path(path)), codegenReceiptSha256=digest(report))
            if progress:
                progress(f'[ps2-codegen] module={number:02} kind={label} accepted')
        if 1 <= number <= 15:
            stages.append(dict(id=number, generated=str(directory / 'base-generated'),
                               receipt=str(directory / 'base-codegen.json'), prepared=str(output / 'disc/prepared.json'),
                               globalRootAliases=module.get('globalRootAliases', False)))
        if 'extra' in module:
            item = {k: v for k, v in module['extra'].items() if k not in ('map', 'mapSha256', 'rows', 'general')}
            extras.append(dict(item, id=number, base=str(directory / 'base-generated'),
                               generated=str(directory / 'extra-generated'), map=str(directory / 'extra.csv'),
                               baseReceipt=str(directory / 'base-codegen.json'), receipt=str(directory / 'extra-codegen.json')))
    tables = [s['generated'] + '/register_functions.cpp' for s in stages]
    for name, value in [('stage-modules.json', dict(format=1, modules=stages)),
                        ('mode-extras.json', dict(format=1, modules=extras, checkLinks=True, linkedTables=tables))]:
        write_changed(output / name,json.dumps(value, indent=2) + '\n')
    return dict(boot=str(output / 'module-00/base-generated'), arcade=str(output / 'module-16/base-generated'),
                arrange=str(output / 'module-17/base-generated'), stages=str(output / 'stage-modules.json'),
                extras=str(output / 'mode-extras.json'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, help='Original SLPS-25352 ISO; omit to resume generation')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--find-iso', type=Path, help='Print the supported ISO in a data directory')
    parser.add_argument('--default-output', action='store_true', help='Print the fingerprinted build input directory')
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--progress', action='store_true')
    parser.add_argument('--refresh-tool', action='store_true', help='Validate exact source equivalence after a tool relink')
    args = parser.parse_args()
    if args.default_output:
        print(default_output())
        raise SystemExit(0)
    if args.find_iso:
        print(find_iso(args.find_iso))
        raise SystemExit(0)
    if not args.output:
        parser.error('--output is required')
    if args.iso:
        prepare(args.iso, args.output)
    if not args.prepare_only:
        progress=(lambda text: print(text,file=sys.stderr,flush=True)) if args.progress else None
        print(json.dumps(generate_all(args.output,progress,refresh=args.refresh_tool), indent=2))
