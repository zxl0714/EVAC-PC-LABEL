"""Stage a fresh G2 latency fixture with test-only settings and profile."""
import argparse
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from generate_catalogue import load
from project import digest, write_json_new


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--destination', type=Path, required=True)
    p.add_argument('--frame-delay', type=int, choices=(1, 2), default=2)
    p.add_argument('--profile', type=Path)
    a = p.parse_args()
    private = ROOT / '.local/input-latency-v080-r1'
    build, destination = a.build.resolve(), a.destination.resolve()
    if not all(path.is_relative_to(private) for path in (build, destination)):
        raise ValueError('Private build/stage required')
    if not (build / 'build-result.json').is_file():
        raise ValueError('Successful completed build receipt required')
    baseline = ROOT / '.local/v080-g2-latency-r1/collection'
    overrides = {
        'espgaluda2.exe': build / 'espgaluda2.exe',
        'espgaluda2_dat12.dll': build / 'espgaluda2_dat12.dll',
        'rexruntime.dll': private / 'evac-futari-pixels-build-r2/rexruntime.dll',
    }
    destination.mkdir(parents=True, exist_ok=False)
    game = next(g for g in load(ROOT / 'games/catalogue.json') if g['id'] == 'espgaluda2')
    copied = []
    for name in game['runtimeFiles']:
        if name.endswith('runtime-manifest.ini'):
            continue
        source = overrides.get(Path(name).name, baseline / name)
        output = destination / name
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, output)
        if digest(source) != digest(output):
            raise ValueError('Copy mismatch')
        copied.append(dict(source=str(source), path=name, sha256=digest(output)))
        if source.name in ('espgaluda2.exe', 'espgaluda2_dat12.dll'):
            shutil.copy2(source.with_suffix('.pdb'), output.with_suffix('.pdb'))
    user = destination / 'user-data'
    user.mkdir()
    test_profile = a.profile.resolve() if a.profile else baseline / 'user-data/espgaluda2'
    if a.profile and not test_profile.is_relative_to(private):
        raise ValueError('Only private test profiles may be imported')
    shutil.copytree(test_profile, user / 'espgaluda2')
    profile = []
    for source in test_profile.rglob('*'):
        if source.is_file():
            relative = source.relative_to(test_profile)
            if digest(source) != digest(user / 'espgaluda2' / relative):
                raise ValueError('Test profile copy mismatch')
            profile.append(dict(path=relative.as_posix(), sha256=digest(source)))
    (user / 'config.ini').write_text(f'''[collection]
language = en
selected_game = espgaluda2
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
[game.espgaluda2]
data_dir = E:/Projects/EVAC-PC-LABEL/game-data/espgaluda2
frame_delay = {a.frame_delay}
''', encoding='ascii')
    write_json_new(destination / 'probe-stage.json', dict(files=copied, test_profile=profile,
        original_profile=str(test_profile), original_profile_modified=False,
        release_certified=False, frame_delay=a.frame_delay,
        build_receipt_sha256=digest(build / 'build-result.json')))
    print(destination)


if __name__ == '__main__':
    main()
