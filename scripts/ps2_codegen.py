"""Run the pinned recompiler and validate its report before accepting generated code."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tomllib
import uuid

from project import ROOT, digest, reject_link, write_json_new
from ps4_sources import restore
from ps2_module_extras import guest_branch_targets, table_entries


SDK_LIFECYCLES = (
    ('sceSifAllocIopHeap', 'sceSifFreeIopHeap'),
    ('sceSifAllocSysMemory', 'sceSifFreeSysMemory'),
    ('sceOpen', 'sceClose'), ('sceMcOpen', 'sceMcClose'),
    ('scePadPortOpen', 'scePadPortClose'),
    ('_malloc_r', '_free_r'), ('_calloc_r', '_free_r'),
    ('memalign', '_free_r'), ('__malloc_lock', '__malloc_unlock'),
    ('CreateSema', 'DeleteSema'), ('AddIntcHandler', 'RemoveIntcHandler'),
    ('AddDmacHandler', 'RemoveDmacHandler'),
)


def tree_identity(directory):
    directory = Path(directory)
    reject_link(directory)
    state = hashlib.sha256()
    count = size = 0
    sources = []
    for current, folders, files in os.walk(directory, followlinks=False):
        for name in (*folders, *files):
            child = Path(current) / name
            if child.is_symlink() or child.is_junction():
                raise ValueError('Linked generated source: ' + name)
        sources.extend(Path(current) / name for name in files if Path(name).suffix in ('.h', '.cpp'))
    for path in sorted(sources):
        name = path.relative_to(directory).as_posix()
        state.update(name.encode('utf-8') + b'\0' + bytes.fromhex(digest(path)))
        count += 1
        size += path.stat().st_size
    if not count:
        raise ValueError('Generated PS2 source directory is empty')
    return dict(path=str(directory), files=count, bytes=size, sha256=state.hexdigest())


def equivalence_path(report, tool_hash):
    report = Path(report)
    return report.with_name(report.stem + '-equivalence-' + tool_hash[:20] + '.json')


def verify_equivalence(config, report, tool_hash, root=ROOT):
    config, report = Path(config), Path(report)
    proof_path = equivalence_path(report, tool_hash)
    reject_link(proof_path)
    proof = json.loads(proof_path.read_text(encoding='utf-8'))
    if (type(proof.get('format')) is not int or proof['format'] != 1 or
            proof.get('kind') != 'identical-generated-sources' or proof.get('toolSha256') != tool_hash or
            proof.get('originalConfigSha256') != digest(config) or proof.get('originalReceiptSha256') != digest(report)):
        raise ValueError('Generated-source equivalence belongs to different inputs')
    fresh_config, fresh_report = Path(proof['freshConfig']), Path(proof['freshReceipt'])
    for path in (fresh_config, fresh_report):
        reject_link(path)
        if not path.resolve().is_relative_to(Path(root).resolve()):
            raise ValueError('Generated-source equivalence escapes the project')
    if digest(fresh_config) != proof['freshConfigSha256'] or digest(fresh_report) != proof['freshReceiptSha256']:
        raise ValueError('Fresh generation evidence changed')
    original = tomllib.loads(config.read_text(encoding='utf-8'))
    fresh = tomllib.loads(fresh_config.read_text(encoding='utf-8'))
    fresh_output = fresh['general']['output']
    for path in (Path(fresh_output), Path(original['general']['output'])):
        if not path.resolve().is_relative_to(Path(root).resolve()):
            raise ValueError('Generated-source equivalence escapes the project')
    fresh['general']['output'] = original['general']['output']
    if fresh != original:
        raise ValueError('Fresh generation changed more than its output directory')
    checked = json.loads(fresh_report.read_text(encoding='utf-8'))
    previous = json.loads(report.read_text(encoding='utf-8'))
    if (checked.get('toolSha256') != tool_hash or checked.get('configSha256') != digest(fresh_config) or
            any(checked.get(key) != previous.get(key) for key in
                ('elfSha256', 'functionMapSha256', 'sdkBindingsSha256')) or
            any(checked.get('summary', {}).get(key) != 0 for key in
                ('errors', 'decodeFailures', 'unhandledInstructions', 'criticalGuestFallbacks'))):
        raise ValueError('Fresh generation did not validate the same original inputs')
    original_tree = tree_identity(Path(original['general']['output']))
    for key in ('files', 'bytes', 'sha256'):
        # Only the original tree is compiled. The separate, freshly generated
        # tree was compared in full when this immutable proof was produced.
        if original_tree[key] != proof['sourceTree'][key] or original_tree[key] != proof['freshSourceTree'][key]:
            raise ValueError('Generated game bodies changed; use the fresh build inputs')
    return dict(original_tree, codegenReceiptSha256=digest(report), equivalenceProofSha256=digest(proof_path),
                toolSha256=tool_hash)


def refresh_tool(config, report, root=ROOT):
    """Re-run a changed tool; preserve compiled bodies only on exact equivalence."""
    config, report, root = map(Path, (config, report, root))
    tool_hash = digest(root / 'out/ps2-tools/bin/ps2_recomp.exe')
    proof_path = equivalence_path(report, tool_hash)
    if proof_path.exists():
        return verify_equivalence(config, report, tool_hash, root)
    original_text = config.read_text(encoding='utf-8')
    original_config_hash, original_report_hash = digest(config), digest(report)
    original = tomllib.loads(original_text)
    previous = json.loads(report.read_text(encoding='utf-8'))
    recovered = None
    # An interrupted comparison may already have a completed fresh generation.
    # Reuse that evidence rather than generating another copy of every body.
    for candidate in sorted(report.parent.glob(report.stem + '-refresh-*')):
        fresh_config, fresh_report = candidate / 'config.toml', candidate / 'codegen.json'
        for path in (candidate, fresh_config, fresh_report):
            reject_link(path)
        if not fresh_config.is_file() or not fresh_report.is_file():
            continue
        record = json.loads(fresh_report.read_text(encoding='utf-8'))
        parsed = tomllib.loads(fresh_config.read_text(encoding='utf-8'))
        destination = (candidate / 'generated').resolve()
        if Path(parsed['general']['output']).resolve() != destination:
            continue
        parsed['general']['output'] = original['general']['output']
        if (parsed == original and record.get('configSha256') == digest(fresh_config) and
                record.get('toolSha256') == tool_hash and
                all(record.get(key) == previous.get(key) for key in
                    ('elfSha256', 'functionMapSha256', 'sdkBindingsSha256')) and
                all(record.get('summary', {}).get(key) == 0 for key in
                    ('errors', 'decodeFailures', 'unhandledInstructions', 'criticalGuestFallbacks'))):
            recovered = fresh_config, fresh_report, destination, record
            break
    if recovered:
        fresh_config, fresh_report, destination, generated = recovered
    else:
        directory = report.parent / (report.stem + '-refresh-' + uuid.uuid4().hex[:12])
        reject_link(directory)
        directory.mkdir()
        destination = (directory / 'generated').resolve()
        fresh_text, count = re.subn(r'(?m)^output\s*=.*$',
            lambda _: 'output = ' + json.dumps(destination.as_posix()), original_text)
        parsed = tomllib.loads(fresh_text)
        parsed['general']['output'] = original['general']['output']
        if count != 1 or parsed != original:
            raise ValueError('Cannot isolate the generation output setting')
        fresh_config, fresh_report = directory / 'config.toml', directory / 'codegen.json'
        fresh_config.write_text(fresh_text, encoding='utf-8', newline='\n')
        generated = generate(fresh_config, directory / 'codegen.log', fresh_report, root)
    if any(generated.get(key) != previous.get(key) for key in
           ('elfSha256', 'functionMapSha256', 'sdkBindingsSha256')):
        raise ValueError('Original inputs changed while refreshing the tool')
    before, after = tree_identity(Path(original['general']['output'])), tree_identity(destination)
    if any(before[key] != after[key] for key in ('files', 'bytes', 'sha256')):
        raise ValueError('Generated game bodies changed; use the fresh build inputs')
    if digest(root / 'out/ps2-tools/bin/ps2_recomp.exe') != tool_hash:
        raise ValueError('Recompiler changed during equivalence validation')
    if digest(config) != original_config_hash or digest(report) != original_report_hash:
        raise ValueError('Original generation evidence changed during equivalence validation')
    write_json_new(proof_path, dict(format=1, kind='identical-generated-sources', toolSha256=tool_hash,
        originalConfigSha256=original_config_hash, originalReceiptSha256=original_report_hash,
        freshConfig=str(fresh_config.resolve()), freshConfigSha256=digest(fresh_config),
        freshReceipt=str(fresh_report.resolve()), freshReceiptSha256=digest(fresh_report),
        sourceTree=before, freshSourceTree=after))
    return dict(before, codegenReceiptSha256=digest(report), equivalenceProofSha256=digest(proof_path),
                toolSha256=tool_hash)


def validate_sdk_lifecycles(general, source):
    names={selector.rsplit('@',1)[0] for selector in general.get('stubs',[])}
    names.update(re.findall(r'BindSdkHandler\(runtime,0x[0-9a-fA-F]+u,"([^"]+)"\)',source))
    checked=[]
    for acquire,release in SDK_LIFECYCLES:
        if acquire not in names:
            continue
        if release not in names:
            raise ValueError(f'Incomplete native SDK lifecycle: {acquire} requires {release}')
        checked.append(dict(acquire=acquire,release=release))
    return checked


def validate_static_edges(generated, elf):
    """Reject missing constant branches and reachable translated-block tails.

    A plain generated C++ tail can follow an unconditional JR/ERET return, so
    its presence alone does not establish a reachable guest continuation.
    Callback and computed-jump closure still requires separate game analysis.
    """
    generated = Path(generated)
    image = Path(elf).read_bytes()
    if len(image) < 52 or image[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Static-edge validation requires a little-endian ELF32')
    phoff = struct.unpack_from('<I', image, 28)[0]
    phsize, phcount = struct.unpack_from('<HH', image, 42)
    if phsize < 32 or phoff + phsize*phcount > len(image):
        raise ValueError('Invalid ELF program headers')
    segments = []
    for index in range(phcount):
        kind, offset, address, _, size, _, _, _ = struct.unpack_from('<8I', image, phoff+index*phsize)
        if kind == 1:
            if offset+size > len(image):
                raise ValueError('ELF load segment exceeds the file')
            segments.append((address, address+size, offset))

    def instruction(address):
        for begin, end, offset in segments:
            if begin <= address and address+4 <= end:
                return struct.unpack_from('<I', image, offset+address-begin)[0]
        raise ValueError(f'Generated tail has no original instruction: 0x{address:x}')

    linked = table_entries(generated/'register_functions.cpp')
    if not linked:
        raise ValueError('Static-edge validation requires a generated function table')
    missing = []; bodies = edges = tails = 0
    for source in generated.glob('*.cpp'):
        if source.name == 'register_functions.cpp':
            continue
        bodies += 1
        text = source.read_text(encoding='utf-8')
        targets = guest_branch_targets(text)
        tail = re.search(r'ctx->pc\s*=\s*0x([0-9a-fA-F]+)u;\s*}\s*$', text)
        if tail:
            address = int(tail[1], 16)
            header = re.search(r'// Address: 0x([0-9a-fA-F]+) - ', text)
            if not header:
                raise ValueError('Generated tail has no function boundary: '+source.name)
            begin = int(header[1], 16)
            last = instruction(address-4)
            previous = instruction(address-8) if address-begin >= 8 else 0
            kind = previous >> 26
            # ERET has no delay slot. J/JR and BEQ with identical operands
            # always transfer control after theirs; the emitted tail is dead.
            terminal = (last == 0x42000018 or kind == 2 or
                        (kind == 0 and previous & 63 == 8) or
                        (kind == 4 and ((previous >> 21) & 31) == ((previous >> 16) & 31)))
            if not terminal:
                targets.add(address)
                tails += 1
        edges += len(targets)
        missing.extend(f'{source.stem}:0x{target:x}' for target in sorted(targets-linked.keys()))
    if missing:
        raise ValueError('Missing statically compiled guest edges: '+', '.join(missing[:24]))
    return dict(bodies=bodies, constantEdges=edges, reachableTails=tails)


def validate_sdk_bindings(general, source):
    """A dispatch-table replacement cannot intercept a generated direct call."""
    bindings=re.findall(r'BindSdkHandler\(runtime,0x([0-9a-fA-F]+)u,"([^"]+)"\)',source)
    if not bindings:
        raise ValueError('No explicit native SDK bindings found')
    selectors={}
    for selector in general.get('stubs',[]):
        name,separator,address=selector.rpartition('@')
        if separator:
            number=int(address,0)
            if number in selectors and selectors[number]!=name:
                raise ValueError('Conflicting SDK handler names at '+address)
            selectors[number]=name
    for address,name in bindings:
        if selectors.get(int(address,16))!=name:
            raise ValueError(f'Native SDK direct call lacks a generated binding: {name}@0x{address}')
    return len(bindings)


def validate_sdk_map(source, map_text):
    """Every bound native entry must exist in the generated address table."""
    starts = {int(row['Start'], 0) for row in csv.DictReader(map_text.splitlines())}
    bindings = re.findall(r'BindSdkHandler\(runtime,0x([0-9a-fA-F]+)u,"([^"]+)"\)', source)
    missing = [f'{name}@0x{address}' for address, name in bindings if int(address, 16) not in starts]
    if missing:
        raise ValueError('SDK entries absent from function map: ' + ', '.join(missing))
    return len(bindings)


def translation_summary(text):
    patterns = {
        'discovered': r'^Functions discovered: (\d+)$',
        'processed': r'^Functions processed: (\d+), recompiled: (\d+), stubs: (\d+), skipped: (\d+), decode failures: (\d+)$',
        'generated': r'^Generated functions: (\d+)$',
        'unhandled': r'^Unhandled instructions: (\d+)$',
        'critical': r'^Correctness-critical guest fallbacks: (\d+), failures: (\d+)$',
        'diagnostics': r'^Warnings: (\d+), errors: (\d+)$',
    }
    fields = {}
    for name, pattern in patterns.items():
        matches = re.findall(pattern, text.replace('\r\n', '\n'), re.MULTILINE)
        if len(matches) != 1:
            raise ValueError(f'Missing or ambiguous PS2 recompiler report: {name}')
        value = matches[0]
        fields[name] = tuple(map(int, value)) if isinstance(value, tuple) else int(value)
    processed, compiled, stubbed, skipped, decode_failures = fields['processed']
    if (fields['discovered'] == 0 or processed != fields['discovered']
            or processed != compiled+stubbed+skipped or fields['generated'] != processed
            or skipped or decode_failures or fields['unhandled']
            or fields['critical'][1] or fields['diagnostics'][1]):
        raise ValueError(f'PS2 translation is incomplete or has errors: {fields}')
    return dict(functions=processed, recompiled=compiled, stubs=stubbed,
                warnings=fields['diagnostics'][0], criticalGuestFallbacks=fields['critical'][0],
                unhandledInstructions=0, decodeFailures=0, errors=0)


def generate(config, log, receipt, root=ROOT):
    root, config, log, receipt = map(lambda p: Path(p).absolute(), (root, config, log, receipt))
    for path in (config, log, receipt):
        reject_link(path)
    if log.exists() or receipt.exists():
        raise FileExistsError('Existing generation evidence is preserved')
    restore(root, verify_only=True, backend='ps2')
    tool = root/'out/ps2-tools/bin/ps2_recomp.exe'
    reject_link(tool)
    general = tomllib.loads(config.read_text(encoding='utf-8'))['general']
    inputs = {}
    input_keys=['input','ghidra_output']
    if 'sdk_bindings' in general:
        input_keys.append('sdk_bindings')
    for key in input_keys:
        value = general.get(key)
        if not isinstance(value, str) or not value:
            raise ValueError(f'PS2 game generation requires an explicit {key}')
        path = Path(value)
        if not path.is_absolute():
            path = root/path
        reject_link(path)
        inputs[key] = dict(path=path, sha256=digest(path))
    if 'sdk_bindings' in inputs:
        bindings_source = inputs['sdk_bindings']['path'].read_text(encoding='utf-8')
        validate_sdk_bindings(general, bindings_source)
        sdk_lifecycles=validate_sdk_lifecycles(general, bindings_source)
        # Sparse runtime-only entries remain valid for existing builds. Games
        # that request a complete direct SDK table can require it explicitly.
        if general.get('require_sdk_map_entries', False):
            validate_sdk_map(bindings_source, inputs['ghidra_output']['path'].read_text(encoding='utf-8'))
    destination = Path(general['output'])
    if not destination.is_absolute():
        destination = root/destination
    reject_link(destination)
    relative = destination.resolve().relative_to(root.resolve())
    if not relative.parts or relative.parts[0] not in ('.local', 'generated', 'out'):
        raise ValueError('Generated game code must use a project build directory')
    if destination.exists() and (not destination.is_dir() or next(destination.iterdir(), None) is not None):
        raise FileExistsError('Use an empty generation directory; existing outputs are preserved')
    config_hash, tool_hash = digest(config), digest(tool)
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open('xb') as output:
        result = subprocess.run([str(tool), str(config)], cwd=root,
                                stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT,
                                timeout=600)
    if result.returncode:
        raise RuntimeError(f'PS2 recompiler exited {result.returncode}; see {log}')
    # The upstream CLI can return zero despite unhandled instructions or output
    # errors. Its version is pinned; a changed report format must be reviewed.
    summary = translation_summary(log.read_text(encoding='utf-8', errors='replace'))
    static_edges = (validate_static_edges(destination, inputs['input']['path'])
                    if general.get('require_static_edges', False) else None)
    if digest(config) != config_hash or digest(tool) != tool_hash:
        raise ValueError('Recompiler or configuration changed during generation')
    for item in inputs.values():
        if digest(item['path']) != item['sha256']:
            raise ValueError('ELF or function map changed during generation')
    value = dict(format=1, configSha256=config_hash, toolSha256=tool_hash,
                 elfSha256=inputs['input']['sha256'], functionMapSha256=inputs['ghidra_output']['sha256'],
                 logSha256=digest(log), summary=summary, gameplayVerified=False)
    if 'sdk_bindings' in inputs:
        value['sdkBindingsSha256']=inputs['sdk_bindings']['sha256']
        value['sdkLifecycles']=sdk_lifecycles
    if static_edges is not None:
        value['staticEdges']=static_edges
    write_json_new(receipt, value)
    return value


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config', type=Path)
    parser.add_argument('--log', required=True, type=Path)
    parser.add_argument('--receipt', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(generate(args.config, args.log, args.receipt), indent=2))
