"""Stage offline PS4 latency fixtures without copying player profiles or credentials."""
import argparse
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
PRIVATE = ROOT / '.local/input-latency-v080-r1'
GAMES = ('ketsui', 'daioujou', 'esprade', 'feveron')
DATA = {game: ROOT / 'game-data' / game for game in GAMES}
DATA['feveron'] = ROOT / '.local/v080-feveron-base-r1/uroot'
sys.path.insert(0, str(ROOT / 'scripts'))
from project import digest, write_json_new


def stage(backend, build, destination):
    if not destination.is_relative_to(PRIVATE) or not build.is_relative_to(PRIVATE):
        raise ValueError('Private measurement paths required')
    originals = [dict(game=game, path=str(DATA[game] / 'eboot.bin'),
                      sha256=digest(DATA[game] / 'eboot.bin')) for game in GAMES]
    destination.mkdir(parents=True, exist_ok=False)
    copies = []

    def copy(source, relative):
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        identity = digest(source)
        if digest(target) != identity:
            raise ValueError('Copy mismatch: ' + relative)
        copies.append(dict(source=str(source), path=relative, sha256=identity))

    if backend == 'evac':
        for game in GAMES:
            copy(build / 'KetsuiNativeProbe.exe', f'runtime/ps4/{game}.exe')
        copy(build / 'KetsuiGpuRuntime.dll', 'runtime/ps4/KetsuiGpuRuntime.dll')
        user = destination / 'user-data'
        user.mkdir()
        config = '''[collection]
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
'''
        for game in GAMES:
            config += f'\n[game.{game}]\ndata_dir = {DATA[game].as_posix()}\n'
        (user / 'config.ini').write_text(config, encoding='ascii')
        # The normal launch contract verifies every component. Make a manifest
        # from these actual private binaries, not a copied release certificate.
        for game in GAMES:
            manifest = destination / f'runtime/games/{game}/runtime-manifest.ini'
            manifest.parent.mkdir(parents=True, exist_ok=True)
            names = (f'runtime/ps4/{game}.exe', 'runtime/ps4/KetsuiGpuRuntime.dll')
            text = f'[runtime]\ngame = {game}\ncontract = 1\nsource_commit = measurement-only\n\n[files]\n'
            text += ''.join(f'{name} = {digest(destination / name)}\n' for name in names)
            manifest.write_text(text, encoding='ascii')
    else:
        copy(build / 'shadps4.exe', 'shadps4.exe')
        # A portable user folder must exist before static initialization; the
        # --override-root option is a guest mount override, NOT a profile path.
        user = destination / 'user'
        user.mkdir()
        template = ROOT / '.local/perf-compare-v075/tools/shadps4/user/config.json'
        config = json.loads(template.read_text(encoding='utf-8'))
        config['GPU'].update(full_screen=False, full_screen_mode='Windowed',
            internal_screen_height=720, internal_screen_width=1280,
            window_height=720, window_width=1280, present_mode='Fifo',
            vblank_frequency=60)
        config['General'].update(home_dir='', font_dir='', sys_modules_dir='',
            install_dirs=[], addon_install_dir='', show_splash=False,
            volume_slider=25, discord_rpc_enabled=False)
        config['Network'].update(connected_to_network=False,
            shad_net_enabled=False, enable_upnp=False)
        config['Log'].update(size_limit=104857600)
        config['Vulkan'].update(vkvalidation_enabled=False,
            pipeline_cache_enabled=False)
        write_json_new(user / 'config.json', config)
        # Bind the same physical keyboard layout for the later real frontend
        # probe. Synthetic API tests bypass these bindings and are labelled so.
        bindings = (template.parent / 'input_config/default.ini').read_text(encoding='utf-8')
        for before, after in (('pad_up = up', 'pad_up = i'), ('pad_down = down', 'pad_down = k'),
                              ('pad_left = left', 'pad_left = j'), ('pad_right = right', 'pad_right = l'),
                              ('cross = n', 'cross = z'), ('circle = b', 'circle = x'),
                              ('square = v', 'square = c'), ('triangle = c', 'triangle = v')):
            bindings = bindings.replace(before, after)
        # Remove second-stick keyboard bindings that would duplicate I/J/K/L.
        bindings = '\n'.join(line for line in bindings.splitlines()
                             if not line.startswith(('axis_right_x_minus', 'axis_right_x_plus',
                                                     'axis_right_y_minus', 'axis_right_y_plus'))
                             and line.strip() != 'l3 = x') + '\n'
        (user / 'input_config').mkdir()
        for name in ('default.ini', 'global.ini'):
            (user / 'input_config' / name).write_text(bindings, encoding='utf-8')

    write_json_new(destination / 'probe-stage.json', dict(backend=backend,
        binaries=copies, game_executables=originals, release_certified=False,
        copied_player_profiles=False, generator_sha256=digest(Path(__file__))))
    print(destination)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', choices=('evac', 'shad'), required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    stage(args.backend, args.build.resolve(strict=True), args.destination.resolve())
