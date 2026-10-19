"""Stage only the certified PS4 player runtime and matching symbols; no data."""
import json
import shutil
import uuid

from project import ROOT, digest, reject_link, write_json_new
from ps4_build_receipt import verify


def stage():
    receipt = verify()
    certified = {item['path']: item for item in receipt['outputs']}
    pairs = [('out/ps4-native/KetsuiNativeProbe.exe', 'runtime/ps4/ketsui.exe'),
             ('out/ps4-gpu/KetsuiGpuRuntime.dll', 'runtime/ps4/KetsuiGpuRuntime.dll')]
    outputs = []
    for source_name, target_name in pairs:
        for suffix in (None, '.pdb'):
            source, target = ROOT / source_name, ROOT / target_name
            if suffix:
                source, target = source.with_suffix(suffix), target.with_suffix(suffix)
            reject_link(source); reject_link(target)
            expected = certified[source.relative_to(ROOT).as_posix()]
            if digest(source) != expected['sha256']:
                raise ValueError('PS4 source differs from certified output')
            target.parent.mkdir(parents=True, exist_ok=True)
            if not target.is_file() or digest(target) != expected['sha256']:
                shutil.copy2(source, target)
            if digest(source) != expected['sha256'] or digest(target) != expected['sha256']:
                raise ValueError('PS4 runtime copy failed verification')
            outputs.append(dict(path=target.relative_to(ROOT).as_posix(), bytes=expected['bytes'], sha256=expected['sha256']))
    directory = ROOT / 'runtime/games/ketsui'
    manifest = directory / 'runtime-manifest.json'
    reject_link(manifest); directory.mkdir(parents=True, exist_ok=True)
    if manifest.exists():
        backup = ROOT / '.local/staging' / ('ketsui-' + uuid.uuid4().hex + '-previous.json')
        backup.parent.mkdir(parents=True, exist_ok=True); manifest.rename(backup)
    write_json_new(manifest, dict(format=1, game='ketsui', sourceCommit=receipt['sourceCommit'],
        sourceSnapshotSha256=receipt['sourceSnapshot']['sha256'], files=outputs, gameDataIncluded=False))
    lines = ['[runtime]', 'contract = 1', 'game = ketsui', 'source_commit = ' + receipt['sourceCommit'], '', '[files]']
    lines += [item['path'] + ' = ' + item['sha256'] for item in outputs if not item['path'].endswith('.pdb')]
    certificate = directory / 'runtime-manifest.ini'; reject_link(certificate)
    certificate.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
    return dict(game='ketsui', stagedFiles=len(outputs) + 1, gameDataIncluded=False)


if __name__ == '__main__':
    print(json.dumps(stage(), indent=2))
