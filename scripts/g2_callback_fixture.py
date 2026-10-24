"""Materialize a generated overlay callback for an isolated native ABI test.

The derived function remains local. No guest image, game loop, device, GPU,
audio or network is loaded by this test; its destination is an authored stub.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

from project import ROOT, reject_link

CALLBACKS = {
    'dat00': ('880EEB40', '880EEA28'),
    'dat11': ('880FB808', '880FB6F0'),
    'dat12': ('880EEA28', '880EE910'),
    'dat15': ('880FC4C8', '880FC3B0'),
    'dat162': ('880EE060', '880EDF48'),
    'dat163': ('880EEB00', '880EE9E8'),
    'dat164': ('880FC4E8', '880FC3D0'),
}


def prepare(output, module='dat163'):
    if module not in CALLBACKS:
        raise ValueError('Expected a declared G2 overlay')
    entry, target = CALLBACKS[module]
    reject_link(Path(output).absolute())
    output = Path(output).resolve()
    if not any(output.is_relative_to(ROOT / name) for name in ('.local', 'out')):
        raise ValueError('Derived game code must remain local')
    reject_link(output)
    folder = ROOT / 'generated/espgaluda2' / module
    registry = folder / 'espgaluda2_register.cpp'
    registrations = registry.read_text(encoding='utf-8')
    for address in (entry, target, f'{int(entry, 16) + 16:08X}'):
        if registrations.count(f'SetFunction(0x{address}, sub_{address})') != 1:
            raise ValueError(f'{module} callback and neighbors must be registered exactly once: {address}')
    matches = []
    for source in sorted(folder.glob('espgaluda2_recomp.*.cpp')):
        text = source.read_text(encoding='utf-8')
        match = re.search(r'^DEFINE_REX_FUNC\(sub_' + entry + r'\) \{\n.*?^\}', text, re.M | re.S)
        if match:
            matches.append((source, match[0]))
    if len(matches) != 1:
        raise ValueError('Expected precisely one generated overlay callback')
    source, body = matches[0]
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('w', encoding='utf-8', newline='\n') as stream:
        stream.write(f'#define G2_CALLBACK_ENTRY sub_{entry}\n#define G2_CALLBACK_TARGET sub_{target}\n'
                     'void G2_CALLBACK_TARGET(PPCContext&, std::uint8_t*);\n')
        stream.write(body + '\n')
    return dict(guestExecution=False, gameLoop=False, module=module, callback='0x' + entry,
                target='0x' + target,
                generatedSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                registrySha256=hashlib.sha256(registry.read_bytes()).hexdigest(),
                fixtureSha256=hashlib.sha256(output.read_bytes()).hexdigest())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--module', choices=tuple(CALLBACKS), default='dat163')
    args = parser.parse_args()
    print(json.dumps(prepare(args.output, args.module), indent=2))
