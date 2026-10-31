"""Run the pinned recompiler and validate its report before accepting generated code."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tomllib

from project import ROOT, digest, reject_link, write_json_new
from ps4_sources import restore


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
    for key in ('input', 'ghidra_output'):
        value = general.get(key)
        if not isinstance(value, str) or not value:
            raise ValueError(f'PS2 game generation requires an explicit {key}')
        path = Path(value)
        if not path.is_absolute():
            path = root/path
        reject_link(path)
        inputs[key] = dict(path=path, sha256=digest(path))
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
    if digest(config) != config_hash or digest(tool) != tool_hash:
        raise ValueError('Recompiler or configuration changed during generation')
    for item in inputs.values():
        if digest(item['path']) != item['sha256']:
            raise ValueError('ELF or function map changed during generation')
    value = dict(format=1, configSha256=config_hash, toolSha256=tool_hash,
                 elfSha256=inputs['input']['sha256'], functionMapSha256=inputs['ghidra_output']['sha256'],
                 logSha256=digest(log), summary=summary, gameplayVerified=False)
    write_json_new(receipt, value)
    return value


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config', type=Path)
    parser.add_argument('--log', required=True, type=Path)
    parser.add_argument('--receipt', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(generate(args.config, args.log, args.receipt), indent=2))
