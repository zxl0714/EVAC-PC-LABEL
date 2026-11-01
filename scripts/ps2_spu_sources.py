"""Prepare the pinned Play! DSP component without its emulator save states."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from project import ROOT, reject_link

STATE_METHODS = (
    'CSpuBase::LoadState', 'CSpuBase::SaveState',
    'CSpuIrqWatcher::LoadState', 'CSpuIrqWatcher::SaveState',
    'CSpuBase::CSampleReader::LoadState', 'CSpuBase::CSampleReader::SaveState',
)


def strip_state_methods(text):
    for name in STATE_METHODS:
        pattern = rf'^void {re.escape(name)}\([^\n]*\)(?: const)?\n\{{\n.*?^\}}\n'
        text, count = re.subn(pattern, '', text, flags=re.M | re.S)
        if count != 1:
            raise ValueError(f'Expected one pinned state method: {name}')
    return text


def adapt(name, text):
    if name == 'Iop_SpuBase.cpp':
        text = strip_state_methods(text)
        for header in ('RegisterStateCollectionFile', 'RegisterStateUtils', 'RegisterStateFile'):
            text = text.replace(f'#include "../states/{header}.h"\n', '')
    elif name == 'Iop_SpuBase.h':
        text = text.replace('#include "zip/ZipArchiveWriter.h"\n', '')
        text = text.replace('#include "zip/ZipArchiveReader.h"\n', '')
        text = text.replace('#include <map>\n', '#include <map>\n#include <vector>\n#include <memory>\n')
        text = re.sub(r'^\s*void (?:LoadState|SaveState)\([^\n]*\)(?: const)?;\n', '', text, flags=re.M)
    elif name == 'Log.h':
        # Retain the upstream no-op logging branch; no frontend/file-system
        # dependency or logging singleton is needed by this release DSP target.
        start = text.index('#if LOGGING_ENABLED\n')
        end = text.index('#else\n', start)
        text = text[end + len('#else\n'):]
        text = text[:text.rindex('#endif')]
        text = '#pragma once\n' + text
    return text


def prepare(dependencies, output, inventory=None):
    dependencies, output = Path(dependencies), Path(output)
    inventory = inventory or ROOT / 'third_party/ps2/spu/sources.json'
    reject_link(output)
    entries = json.loads(Path(inventory).read_text(encoding='utf-8'))['files']
    staged = []
    for entry in entries:
        source, target = dependencies / entry['source'], output / entry['target']
        if '..' in Path(entry['source']).parts or '..' in Path(entry['target']).parts:
            raise ValueError('SPU inventory paths must stay within their roots')
        reject_link(source)
        data = source.read_bytes()
        if hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise ValueError(f'Pinned SPU input differs: {source}')
        if source.suffix in ('.cpp', '.h'):
            data = adapt(source.name, data.decode('utf-8').replace('\r\n', '\n')).encode('utf-8')
        staged.append((target, data))
    # Validate every source before producing any adapted output.
    for target, data in staged:
        reject_link(target)
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
    return {'files': len(staged), 'removedStateMethods': list(STATE_METHODS)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dependencies', type=Path, default=ROOT / '.deps/ps2')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.dependencies, args.output)))
