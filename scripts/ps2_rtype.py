"""Build the reviewed SLUS-20780 boot program and native code banks from its ISO."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
from tempfile import TemporaryDirectory

from analysis_paths import private_output
from project import ROOT, digest, reject_link, write_json_new
from ps2_codegen import equivalence_path, generate, refresh_tool, tree_identity, validate_static_edges, verify_equivalence
from ps2_compact import POLICY as COMPACT_POLICY, load_words
from ps2_disc import Iso9660, inspect as inspect_disc
from ps2_game import config_text, read_map
from ps2_module import analysis_elf, inspect as inspect_module
from ps2_module_extras import table_entries
from ps2_translation_units import write_changed

GAME = 'rtype-final'
BOOT = 'SLUS_207.80'
CORE = 1
BANKS = (CORE, *range(4, 16), *range(17, 23), 33, 34, 35, 37, 38)
METADATA = ROOT / 'games' / GAME / 'analysis/manifest.json'
TOOL = ROOT / 'out/ps2-tools/bin/ps2_recomp.exe'
VU_TOOL = TOOL.with_name('ps2_vu_aot.exe')


def metadata(path=METADATA):
    data = json.loads(path.read_text(encoding='utf-8'))
    programs = data['programs']
    if (data.get('format') != 1 or data.get('game') != GAME or
            [item.get('bank') for item in programs] != [None, *BANKS] or
            programs[0]['discPath'] != BOOT or data['dataBank']['bank'] != 2):
        raise ValueError('Expected the complete reviewed SLUS-20780 program inventory')
    for item in programs:
        expected = 'boot' if 'bank' not in item else f"bank-{item['bank']:02}"
        if (item['name'] != expected or item['map'] != expected + '.csv.gz' or
                {'input', 'output', 'ghidra_output', 'sdk_bindings'} & item.get('general', {}).keys()):
            raise ValueError('Invalid R-Type Final analysis metadata')
    return data


def analysis_sources(root=ROOT):
    path = Path(root) / METADATA.relative_to(ROOT)
    if not path.exists():
        return {}
    return {(path.parent / item['map']).relative_to(root).as_posix(): read_map(path.parent, item)
            for item in metadata(path)['programs']}


def default_output():
    sources = [METADATA, Path(__file__), TOOL, ROOT / 'scripts/ps2_codegen.py',
               ROOT / 'scripts/ps2_compact.py', ROOT / 'scripts/ps2_module.py']
    fingerprint = hashlib.sha256(''.join(digest(path) for path in sources).encode()).hexdigest()[:20]
    return ROOT / '.local/ps2' / GAME / fingerprint


def find_iso(path):
    path = Path(path).absolute()
    reject_link(path)
    candidates = [path] if path.is_file() else list(path.glob('*.iso'))
    if len(candidates) > 64:
        raise ValueError('Select the R-Type Final ISO or its containing directory')
    expected = metadata()['programs'][0]['elfSha256']
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
        raise ValueError('Select one supported R-Type Final SLUS-20780 ISO')
    return matches[0]


def program_config(item, folder):
    options = dict(output_worker_threads=2, single_file_output=False,
                   patch_syscalls=False, patch_cop0=True, patch_cache=True, compact_entries=True,
                   **item.get('general', {}))
    return config_text(options, folder / 'program.elf', folder / 'generated', folder / 'functions.csv')


def bank_elf(raw, bank, item):
    low = bank.address + 64
    high = low + bank.text_size
    initialized = raw[64 + bank.text_size:64 + bank.text_size + bank.data_size]
    roots = set()
    # In these banks the initialized data contains aligned pointers to code.
    # Include recognizable stack prologues and bare return entry points.
    for address, in struct.iter_unpack('<I', initialized):
        if address % 16 == 0 and low <= address < high:
            instruction, = struct.unpack_from('<I', raw, address - bank.address)
            if instruction == 0x03e00008 or instruction & 0xffff8000 in (0x27bd8000, 0x67bd8000):
                roots.add(address)
    return analysis_elf(raw, item['entry'], roots)


def prepare(iso, output):
    iso = Path(iso).absolute()
    reject_link(iso)
    iso = iso.resolve()
    output = private_output(output, iso)
    spec = metadata()
    programs = spec['programs']
    images = {}
    with iso.open('rb') as stream:
        disc = Iso9660(stream, iso.stat().st_size)
        entries = disc.entries()
        for item in [*programs, spec['dataBank']]:
            entry = entries.get(item['discPath'])
            if entry is None:
                raise ValueError('Missing original disc file: ' + item['discPath'])
            raw = disc.contents(entry)
            if hashlib.sha256(raw).hexdigest() != item['discSha256']:
                raise ValueError('Disc file differs from SLUS-20780: ' + item['discPath'])
            if 'bank' in item:
                bank = inspect_module(raw)
                if (bank.module_id, bank.address, bank.text_size, bank.data_size, bank.bss_size) != tuple(
                        item[key] for key in ('bank', 'address', 'textSize', 'dataSize', 'bssSize')):
                    raise ValueError('Unexpected disc module layout: ' + item['discPath'])
                if item is spec['dataBank']:
                    if any(raw[64:64 + bank.text_size]):
                        raise ValueError('The data bank contains executable bytes')
                    continue
                raw = bank_elf(raw, bank, item)
            if hashlib.sha256(raw).hexdigest() != item['elfSha256']:
                raise ValueError('Program view differs from the reviewed input: ' + item['name'])
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


def verify_callbacks(item, image, table):
    # Object constructors also install some callbacks directly into writable
    # object fields. These addresses never appear in a static pointer table.
    for target in item.get('objectCallbacks', []):
        if target not in table:
            raise ValueError(f'{item["name"]} object callback {target:#x} has no native entry')
    intervals = item.get('callbackTables', [])
    if not intervals:
        return
    word = load_words(image)
    data_start = item['address'] + 64 + item['textSize']
    data_end = data_start + item['dataSize']
    for first, end in intervals:
        if (first | end) & 3 or not data_start <= first < end <= data_end:
            raise ValueError('R-Type Final callback table is outside initialized data')
        for address in range(first, end, 4):
            target = word(address)
            if target not in table:
                raise ValueError(f'{item["name"]} callback at {address:#x} has no native entry: {target:#x}')


def verify_object_tables(spec, image, tables):
    # The resident object descriptors can refer to code in the active overlay.
    # Each reviewed overlay must close its own references at shared addresses.
    word = load_words(image)
    core = spec['programs'][1]
    data_start = core['address'] + 64 + core['textSize']
    data_end = data_start + core['dataSize']
    resident = tables[None] | tables[CORE]
    for table in spec['objectTables']:
        first, end = (int(table[key], 0) for key in ('first', 'end'))
        stride, offset = (table[key] for key in ('stride', 'callbackOffset'))
        if ((first | end | stride | offset) & 3 or not data_start <= first < end <= data_end or
                offset < 0 or stride < offset + 12 or (end - first) % stride):
            raise ValueError('Invalid R-Type Final object descriptor table')
        count = (end - first) // stride
        overlay_rows = {}
        for bank, rows in table['overlayRows'].items():
            bank = int(bank)
            if bank == CORE or bank not in BANKS or any(not 0 <= row < count for row in rows):
                raise ValueError('Invalid R-Type Final object descriptor overlay')
            for row in rows:
                overlay_rows.setdefault(row, []).append(bank)
        for index, address in enumerate(range(first + offset, end, stride)):
            for bank in overlay_rows.get(index, (CORE,)):
                for callback in range(address, address + 12, 4):
                    target = word(callback)
                    if target not in resident and target not in tables[bank]:
                        raise ValueError(f'R-Type Final object callback {callback:#x} ({target:#x}) '
                                         f'has no native entry in bank {bank}')


def vu_sources(output, create=False):
    images = [output / item['name'] / 'program.elf' for item in metadata()['programs']]
    source = dict(format=2, images={image.parent.name: digest(image) for image in images},
                  compilerSha256=digest(VU_TOOL))
    key = hashlib.sha256(json.dumps(source, sort_keys=True).encode()).hexdigest()[:24]
    destination = output / ('vu1-' + key)
    reject_link(destination)
    if create and not destination.exists():
        environment = {key: value for key, value in os.environ.items()
                       if key.upper() not in ('PS2X_VU_AOT_NO_JIT', 'PS2X_VU_AOT_FAST', 'PS2X_VU_AOT_RELAXED')}
        with TemporaryDirectory(dir=output, prefix='.vu1-') as work:
            staged = Path(work) / 'compiled'
            compiled = staged / 'generated'
            compiled.mkdir(parents=True)
            for image in images:
                # The generator cleans its output. Scan each bank separately,
                # then collect the content-addressed fragments in one registry.
                bank = Path(work) / image.parent.name
                subprocess.run([str(VU_TOOL), str(image), str(bank)], check=True, env=environment,
                               stdout=subprocess.PIPE)
                for fragment in bank.glob('*.cpp'):
                    target = compiled / fragment.name
                    if target.exists():
                        # The leading comment records the ELF offset of the upload.
                        if target.read_bytes().partition(b'\n')[2] != fragment.read_bytes().partition(b'\n')[2]:
                            raise ValueError('Conflicting VU fragment: ' + fragment.name)
                    else:
                        fragment.rename(target)
            identity = tree_identity(compiled)
            del identity['path']
            write_json_new(staged / 'receipt.json', dict(source=source, compiled=identity))
            try:
                staged.rename(destination)
            except OSError:
                if not destination.is_dir():
                    raise
    receipt_path = destination / 'receipt.json'
    reject_link(receipt_path)
    receipt = json.loads(receipt_path.read_text(encoding='utf-8'))
    compiled = destination / 'generated'
    identity = tree_identity(compiled)
    del identity['path']
    if receipt != dict(source=source, compiled=identity):
        raise ValueError('R-Type Final VU1 sources differ from the build receipt')
    return compiled, receipt


def generate_all(output, refresh=False, source_identities=None, create_vu=False):
    output = Path(output).resolve()
    plan = json.loads((output / 'plan.json').read_text(encoding='utf-8'))
    if (plan.get('format') != 1 or plan.get('game') != GAME or
            plan.get('metadataSha256') != digest(METADATA) or plan.get('output') != str(output)):
        raise ValueError('R-Type Final analysis inputs changed; prepare a fresh build directory')
    spec = metadata()
    programs = spec['programs']
    generated = {}
    banks = []
    tables = {}
    tool = digest(TOOL)
    for item in programs:
        name = item['name']
        folder = output / name
        config, receipt = folder / 'config.toml', folder / 'codegen.json'
        directory = folder / 'generated'
        if (config.read_text(encoding='utf-8') != program_config(item, folder) or
                digest(folder / 'program.elf') != item['elfSha256'] or
                digest(folder / 'functions.csv') != item['mapSha256']):
            raise ValueError('Prepared R-Type Final inputs differ: ' + name)
        report = (json.loads(receipt.read_text(encoding='utf-8')) if receipt.exists()
                  else generate(config, folder / 'codegen.log', receipt))
        expected = dict(configSha256=digest(config), elfSha256=item['elfSha256'], functionMapSha256=item['mapSha256'])
        compact = report.get('compaction', {})
        registration = directory / 'register_functions.cpp'
        if (any(report.get(key) != value for key, value in expected.items()) or
                any(report['summary'].get(key) != 0 for key in
                    ('errors', 'decodeFailures', 'unhandledInstructions', 'criticalGuestFallbacks')) or
                compact.get('policy') != COMPACT_POLICY or
                compact.get('toolSha256') != digest(ROOT / 'scripts/ps2_compact.py') or
                compact.get('reportSha256') != digest(folder / 'codegen-compact/report.json') or
                not registration.is_file()):
            raise ValueError('Incomplete or changed R-Type Final translation: ' + name)
        identity = None
        if report['toolSha256'] != tool:
            identity = (refresh_tool(config, receipt) if refresh and not equivalence_path(receipt, tool).exists()
                        else verify_equivalence(config, receipt, tool))
        receipt_hash = digest(receipt)
        if source_identities is not None:
            source_identities[name] = identity or dict(tree_identity(directory), codegenReceiptSha256=receipt_hash)
        number = item.get('bank')
        generated[number] = directory
        tables[number] = set(table_entries(registration))
        verify_callbacks(item, folder / 'program.elf', tables[number])
        if number is not None:
            banks.append(dict(id=number, address=item['address'], textSize=item['textSize'], dataSize=item['dataSize'],
                              bssSize=item['bssSize'], generated=str(directory), receiptSha256=receipt_hash))

    verify_object_tables(spec, generated[CORE].parent / 'program.elf', tables)
    all_banks = set().union(*(tables[number] for number in BANKS))
    edges = {}
    for item in programs:
        number = item.get('bank')
        allowed = all_banks if number is None else (
            tables[None] | (all_banks if number == CORE else tables[CORE]))
        directory = generated[number]
        edges[item['name']] = validate_static_edges(directory, directory.parent / 'program.elf', allowed)
    bank_manifest = output / 'banks.json'
    write_changed(bank_manifest, json.dumps(dict(format=1, banks=banks), indent=2) + '\n')
    write_changed(output / 'static-edges.json', json.dumps(edges, indent=2) + '\n')
    vu, _ = vu_sources(output, create=create_vu)
    return dict(boot=str(generated[None]), banks=str(bank_manifest), vu=str(vu))


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
        print(json.dumps(generate_all(output, create_vu=True)))
