"""Stage a fresh private latency fixture; never overwrite a player installation."""
import argparse
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from generate_catalogue import load
from project import digest, write_json_new


def stage(build, destination, profile, runtime_override=None):
    if not destination.is_relative_to(ROOT / '.local/input-latency-v080-r1'):
        raise ValueError('Private latency fixture root required')
    profile_files_to_copy = native_test_profile(profile)
    if runtime_override and (not runtime_override.is_relative_to(ROOT / '.local/input-latency-v080-r1')
                             or runtime_override.name != 'rexruntime.dll'):
        raise ValueError('Private runtime override required')
    destination.mkdir(parents=True, exist_ok=False)
    game = next(g for g in load(ROOT / 'games/catalogue.json') if g['id'] == 'futari')
    files = []
    for name in game['runtimeFiles']:
        if name.endswith('runtime-manifest.ini'):
            continue
        source, output = build / Path(name).name, destination / name
        if runtime_override and source.name == 'rexruntime.dll': source = runtime_override
        if not source.is_file():
            raise ValueError('Missing built component: ' + str(source))
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, output)
        if digest(source) != digest(output):
            raise ValueError('Copy identity mismatch')
        files.append(dict(path=name, sha256=digest(output)))
        if source.name in ('futari.exe', 'futari_reco_abnormal.dll'):
            shutil.copy2(source.with_suffix('.pdb'), output.with_suffix('.pdb'))
    # This is an explicitly un-certified private fixture. The host's run.json
    # inventories the actual binaries; do not copy a release certificate here.
    user = destination / 'user-data'
    user.mkdir()
    (user / 'config.ini').write_text('''[collection]
language = en
[display]
fullscreen = false
vsync = true
[audio]
volume = 0.25
[community]
enabled = false
nickname = OfflineLatency
[gamepad]
enabled = false
[keyboard]
up = I
down = K
left = J
right = L
[game.futari]
data_dir = E:/Projects/EVAC-PC-LABEL/game-data/futari
''', encoding='ascii')
    # Only a stopped native TEST profile is supported. Xenia's STFS header is
    # not the native host's metadata header even when the save payload matches.
    target = user / 'futari/default/B13EBABEBABEBABE/435A07D7'
    profile_files = []
    for source in profile_files_to_copy:
        relative = source.relative_to(profile)
        copied = target / relative
        copied.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, copied)
        if digest(source) != digest(copied):
            raise ValueError('Test profile copy mismatch')
        profile_files.append(dict(path=relative.as_posix(), sha256=digest(source)))
    write_json_new(destination / 'probe-stage.json', dict(files=files,
        copied_test_profile=str(profile), profile_files=profile_files,
        player_data_modified=False, release_certified=False,
        sources={p.name: digest(p) for p in (Path(__file__), ROOT / 'tools/latency/futari_probe_host.cpp',
                   ROOT / 'tools/latency/futari_probe_exports.h', ROOT / 'tools/latency/futari_probe.cmake')}))
    print(destination)


def native_test_profile(profile):
    if not profile.is_relative_to(ROOT / '.local/input-latency-v080-r1'):
        raise ValueError('Private native TEST profile required')
    payload = profile / '00000001/systemdata.bin/savegame.bin'
    header = profile / 'Headers/00000001/systemdata.bin.header'
    if not payload.is_file() or payload.stat().st_size != 8192:
        raise ValueError('Expected Futari settings payload')
    if not header.is_file() or header.stat().st_size != 328:
        raise ValueError('Expected native settings header; do not import Xenia headers')
    return [payload, header]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--runtime-override', type=Path)
    args = parser.parse_args()
    stage(args.build.resolve(strict=True), args.destination.resolve(), args.profile.resolve(strict=True),
          args.runtime_override.resolve(strict=True) if args.runtime_override else None)
