"""Materialize one generated Novice callback for an isolated native ABI test.

The derived function remains local. No guest image, game loop, device, GPU,
audio or network is loaded by this test; its destination is an authored stub.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

from project import ROOT, reject_link


def prepare(output):
    reject_link(Path(output).absolute())
    output = Path(output).resolve()
    if not any(output.is_relative_to(ROOT / name) for name in ('.local', 'out')):
        raise ValueError('Derived game code must remain local')
    reject_link(output)
    folder = ROOT / 'generated/espgaluda2/dat163'
    registry = folder / 'espgaluda2_register.cpp'
    if registry.read_text(encoding='utf-8').count('SetFunction(0x880EEB00, sub_880EEB00)') != 1:
        raise ValueError('Novice callback must be registered exactly once')
    matches = []
    for source in sorted(folder.glob('espgaluda2_recomp.*.cpp')):
        text = source.read_text(encoding='utf-8')
        match = re.search(r'^DEFINE_REX_FUNC\(sub_880EEB00\) \{\n.*?^\}', text, re.M | re.S)
        if match:
            matches.append((source, match[0]))
    if len(matches) != 1:
        raise ValueError('Expected precisely one generated Novice callback')
    source, body = matches[0]
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('w', encoding='utf-8', newline='\n') as stream:
        stream.write(body + '\n')
    return dict(guestExecution=False, gameLoop=False, callback='0x880EEB00',
                generatedSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                registrySha256=hashlib.sha256(registry.read_bytes()).hexdigest(),
                fixtureSha256=hashlib.sha256(output.read_bytes()).hexdigest())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    print(json.dumps(prepare(parser.parse_args().output), indent=2))
