"""Prepare and compile Ibara's reviewed PS2 boot program."""
import argparse
import hashlib
import json
from pathlib import Path
import tomllib

from project import ROOT, digest, reject_link
from ps2_disc import Iso9660, inspect as inspect_disc
from ps2_game import read_map, config_text
from ps2_codegen import generate, tree_identity, refresh_tool, verify_equivalence
from ps2_compact import POLICY as COMPACT_POLICY

METADATA = ROOT / 'games/ibara/analysis/manifest.json'
SDK = ROOT / 'games/ibara/sdk_bindings.cpp'


def metadata(path=METADATA):
    path = Path(path)
    value = json.loads(path.read_text(encoding='utf-8'))
    if value.get('format') != 1 or value.get('game') != 'ibara' or value.get('bootPath') != 'SLPM_663.01':
        raise ValueError('Expected reviewed SLPM-66301 analysis')
    read_map(path.parent, value)
    if any(key in value['general'] for key in ('input', 'output', 'ghidra_output', 'sdk_bindings')):
        raise ValueError('Reviewed Ibara analysis must use portable paths')
    if not value['general'].get('require_static_edges') or not value['general'].get('require_sdk_map_entries'):
        raise ValueError('Ibara generation requires complete SDK and static-edge checks')
    return value


def analysis_sources(root=ROOT):
    path = Path(root) / 'games/ibara/analysis/manifest.json'
    spec = metadata(path)
    return {(path.parent / spec['map']).relative_to(root).as_posix(): read_map(path.parent, spec)}


def default_output():
    sources = [METADATA, Path(__file__), SDK, ROOT / 'scripts/ps2_codegen.py',
               ROOT / 'scripts/ps2_compact.py', ROOT / 'out/ps2-tools/bin/ps2_recomp.exe']
    identity = hashlib.sha256(''.join(digest(path) for path in sources).encode()).hexdigest()[:20]
    return ROOT / '.local/ps2/ibara' / identity


def find_iso(directory):
    directory = Path(directory).resolve()
    files = [directory] if directory.is_file() else list(directory.iterdir())
    if len(files) > 4096:
        raise ValueError('Choose the Ibara data directory')
    expected = metadata()['bootSha256']
    matches = []
    for path in files:
        if path.suffix.lower() != '.iso':
            continue
        reject_link(path)
        try:
            disc = inspect_disc(path)
        except (ValueError, OSError):
            continue
        if disc['candidateGame'] == 'ibara' and disc['elf']['sha256'] == expected:
            matches.append(path)
    if len(matches) != 1:
        raise ValueError('Choose a directory containing one supported Ibara ISO')
    return matches[0]


def prepare(iso, output):
    iso, output = Path(iso).resolve(), Path(output).resolve()
    reject_link(iso); reject_link(output)
    relative = output.relative_to(ROOT.resolve())
    if not relative.parts or relative.parts[0] not in ('.local', 'generated', 'out'):
        raise ValueError('Use a project build directory for generated Ibara code')
    if output.exists():
        raise FileExistsError('Existing preparation is preserved; choose a fresh output')
    spec = metadata()
    with iso.open('rb') as stream:
        disc = Iso9660(stream, iso.stat().st_size)
        raw = disc.contents(disc.entries()[spec['bootPath']])
    if hashlib.sha256(raw).hexdigest() != spec['bootSha256']:
        raise ValueError('Ibara boot program differs from the reviewed version')
    output.mkdir(parents=True)
    (output / 'boot.elf').write_bytes(raw)
    (output / 'functions.csv').write_bytes(read_map(METADATA.parent, spec))
    (output / 'config.toml').write_text(config_text(spec['general'], output / 'boot.elf',
        output / 'generated', output / 'functions.csv', SDK), encoding='utf-8')
    (output / 'plan.json').write_text(json.dumps(dict(format=1, game='ibara', metadataSha256=digest(METADATA),
        output=str(output), iso=str(iso)), indent=2) + '\n', encoding='utf-8')


def generate_all(output, refresh=False, source_identities=None):
    output = Path(output).resolve()
    plan = json.loads((output / 'plan.json').read_text(encoding='utf-8'))
    if (plan.get('format') != 1 or plan.get('game') != 'ibara' or
            plan.get('metadataSha256') != digest(METADATA) or plan.get('output') != str(output)):
        raise ValueError('Ibara analysis inputs changed; prepare a fresh build directory')
    spec = metadata()
    config, receipt = output / 'config.toml', output / 'codegen.json'
    if not receipt.exists():
        generate(config, output / 'codegen.log', receipt)
    report = json.loads(receipt.read_text(encoding='utf-8'))
    general = tomllib.loads(config.read_text(encoding='utf-8'))['general']
    expected = dict(configSha256=digest(config), elfSha256=digest(output / 'boot.elf'),
        functionMapSha256=digest(output / 'functions.csv'), sdkBindingsSha256=digest(SDK))
    if any(report.get(key) != value for key, value in expected.items()):
        raise ValueError('Ibara generated code belongs to different analysis inputs')
    if report['summary'].get('stubs') != len(spec['general']['stubs']):
        raise ValueError('Ibara generation replaced an original SDK routine with an implicit stub')
    if any(report['summary'].get(key) != 0 for key in
            ('errors', 'decodeFailures', 'unhandledInstructions', 'criticalGuestFallbacks')):
        raise ValueError('Ibara code generation is incomplete')
    compact = report.get('compaction', {})
    if (compact.get('policy') != COMPACT_POLICY or compact.get('toolSha256') != digest(ROOT / 'scripts/ps2_compact.py') or
            compact.get('reportSha256') != digest(output / 'codegen-compact/report.json') or not report.get('staticEdges')):
        raise ValueError('Ibara static-code certification is incomplete')
    tool = digest(ROOT / 'out/ps2-tools/bin/ps2_recomp.exe')
    identity = None
    if report.get('toolSha256') != tool:
        identity = refresh_tool(config, receipt) if refresh else verify_equivalence(config, receipt, tool)
    if source_identities is not None:
        source_identities[str(output / 'generated')] = identity or dict(tree_identity(output / 'generated'),
            codegenReceiptSha256=digest(receipt))
    return dict(ibara=str(output / 'generated'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--find-iso', type=Path)
    parser.add_argument('--default-output', action='store_true')
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--refresh-tool', action='store_true')
    args = parser.parse_args()
    if args.find_iso:
        print(find_iso(args.find_iso))
    elif args.default_output:
        print(default_output())
    else:
        if not args.output:
            parser.error('--output is required')
        if args.iso:
            prepare(args.iso, args.output)
        if not args.prepare_only:
            print(json.dumps(generate_all(args.output, refresh=args.refresh_tool), indent=2))
