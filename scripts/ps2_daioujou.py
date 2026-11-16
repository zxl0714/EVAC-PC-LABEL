"""Compile the three reviewed SLPS-25233 programs from the original PS2 disc."""

import argparse
import hashlib
import json
from pathlib import Path

from analysis_paths import private_output
from project import ROOT, digest, reject_link, write_json_new
from ps2_codegen import equivalence_path, generate, refresh_tool, tree_identity, verify_equivalence
from ps2_compact import POLICY as COMPACT_POLICY
from ps2_disc import Iso9660, inspect as inspect_disc
from ps2_game import config_text, read_map


GAME = 'daioujou-ps2'
METADATA = ROOT / 'games' / GAME / 'analysis/manifest.json'
TOOL = ROOT / 'out/ps2-tools/bin/ps2_recomp.exe'
PROGRAMS = {'boot': 'SLPS_252.33', 'death': 'DEATH.ELF', 'gallery': 'GALLERY.ELF'}


def metadata(path=METADATA):
    spec = json.loads(path.read_text(encoding='utf-8'))
    if (spec.get('format') != 1 or spec.get('game') != GAME or
            [(item['name'], item['discPath']) for item in spec['programs']] != list(PROGRAMS.items())):
        raise ValueError('Expected all three reviewed SLPS-25233 programs')
    for item in spec['programs']:
        general = item['general']
        if ('sdk_bindings' in general or {'input', 'output', 'ghidra_output'} & general.keys() or
                not general.get('compact_entries') or not general.get('require_static_edges')):
            raise ValueError('Invalid reviewed DaiOuJou analysis options')
    return spec['programs']


def analysis_sources(root=ROOT):
    path = Path(root) / METADATA.relative_to(ROOT)
    if not path.exists():
        return {}
    return {(path.parent / item['map']).relative_to(root).as_posix(): read_map(path.parent, item)
            for item in metadata(path)}


def default_output():
    sources = [METADATA, Path(__file__), TOOL, ROOT / 'scripts/ps2_codegen.py', ROOT / 'scripts/ps2_compact.py']
    fingerprint = hashlib.sha256(''.join(digest(path) for path in sources).encode()).hexdigest()[:20]
    return ROOT / '.local/ps2' / GAME / fingerprint


def find_iso(directory):
    directory = Path(directory).absolute()
    reject_link(directory)
    candidates = [directory] if directory.is_file() else list(directory.glob('*.iso'))
    if len(candidates) > 64:
        raise ValueError('Choose a smaller directory containing the DaiOuJou ISO')
    expected = metadata()[0]['elfSha256']
    matches = []
    for candidate in candidates:
        reject_link(candidate)
        if not candidate.is_file() or candidate.suffix.lower() != '.iso':
            continue
        try:
            disc = inspect_disc(candidate)
        except (ValueError, OSError):
            continue
        if disc['candidateGame'] == GAME and disc['elf']['sha256'] == expected:
            matches.append(candidate.resolve())
    if len(matches) != 1:
        raise ValueError('Choose one supported SLPS-25233 ISO')
    return matches[0]


def program_config(item, folder):
    return config_text(item['general'], folder / 'program.elf', folder / 'generated', folder / 'functions.csv')


def prepare(iso, output):
    iso = Path(iso).absolute()
    reject_link(iso)
    iso = iso.resolve()
    output = private_output(output, iso)
    programs = metadata()
    images = {}
    with iso.open('rb') as stream:
        disc = Iso9660(stream, iso.stat().st_size)
        entries = disc.entries()
        for item in programs:
            entry = entries.get(item['discPath'])
            if entry is None:
                raise ValueError('Missing disc program: ' + item['discPath'])
            raw = disc.contents(entry)
            if hashlib.sha256(raw).hexdigest() != item['elfSha256']:
                raise ValueError('Disc program differs from the reviewed version: ' + item['discPath'])
            images[item['name']] = raw
    maps = {item['name']: read_map(METADATA.parent, item) for item in programs}
    output.mkdir(parents=True)
    for item in programs:
        name = item['name']
        folder = output / name
        folder.mkdir()
        (folder / 'program.elf').write_bytes(images[name])
        (folder / 'functions.csv').write_bytes(maps[name])
        (folder / 'config.toml').write_text(program_config(item, folder), encoding='utf-8', newline='\n')
    write_json_new(output / 'plan.json', dict(format=1, game=GAME, metadataSha256=digest(METADATA),
                                               output=str(output), iso=str(iso)))


def generate_all(output, refresh=False, source_identities=None):
    output = Path(output).resolve()
    plan = json.loads((output / 'plan.json').read_text(encoding='utf-8'))
    if (plan.get('format') != 1 or plan.get('game') != GAME or
            plan.get('metadataSha256') != digest(METADATA) or plan.get('output') != str(output)):
        raise ValueError('DaiOuJou analysis inputs changed; prepare a fresh build directory')
    result = {}
    tool = digest(TOOL)
    for item in metadata():
        name = item['name']
        folder = output / name
        config, receipt, generated = folder / 'config.toml', folder / 'codegen.json', folder / 'generated'
        if (config.read_text(encoding='utf-8') != program_config(item, folder) or
                digest(folder / 'program.elf') != item['elfSha256'] or
                digest(folder / 'functions.csv') != item['mapSha256']):
            raise ValueError('Prepared DaiOuJou inputs differ: ' + name)
        report = (json.loads(receipt.read_text(encoding='utf-8')) if receipt.exists()
                  else generate(config, folder / 'codegen.log', receipt))
        expected = dict(configSha256=digest(config), elfSha256=item['elfSha256'], functionMapSha256=item['mapSha256'])
        compact = report.get('compaction', {})
        if (any(report.get(key) != value for key, value in expected.items()) or not report.get('staticEdges') or
                any(report['summary'].get(key) != 0 for key in
                    ('errors', 'decodeFailures', 'unhandledInstructions', 'criticalGuestFallbacks')) or
                compact.get('policy') != COMPACT_POLICY or compact.get('toolSha256') != digest(ROOT / 'scripts/ps2_compact.py') or
                compact.get('reportSha256') != digest(folder / 'codegen-compact/report.json') or
                not (generated / 'register_functions.cpp').is_file()):
            raise ValueError('Native DaiOuJou translation is incomplete or differs from its inputs: ' + name)
        identity = None
        if report.get('toolSha256') != tool:
            identity = (refresh_tool(config, receipt) if refresh and not equivalence_path(receipt, tool).exists()
                        else verify_equivalence(config, receipt, tool))
        if source_identities is not None:
            source_identities[name] = identity or dict(tree_identity(generated), codegenReceiptSha256=digest(receipt))
        result[name] = str(generated)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--find-iso', type=Path)
    parser.add_argument('--default-output', action='store_true')
    args = parser.parse_args()
    if args.find_iso:
        print(find_iso(args.find_iso))
    elif args.default_output:
        print(default_output())
    else:
        output = args.output or default_output()
        if args.iso and not output.exists():
            prepare(args.iso, output)
        print(json.dumps(generate_all(output)))
