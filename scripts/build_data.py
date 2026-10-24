"""Resolve build data through the native INI reader, including an Internal pack."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from project import ROOT, config_path


def build_data(game, explicit=None, root=ROOT):
    root = Path(root).resolve()
    if game not in ('espgaluda2', 'futari', 'ketsui', 'sdoj', 'guwange'):
        raise ValueError('Unsupported game build selection')
    if explicit:
        path = Path(explicit).resolve(strict=True)
        if not path.is_dir():
            raise ValueError('Game data must be a directory')
        return path
    # The Internal package keeps one config and one copy of each game's data.
    parent = root.parent
    if root.name.lower() == 'source' and (parent / 'release.json').is_file() and config_path(parent).is_file():
        collection = parent
        inspector = parent / 'runtime/launcher/cave_inspect.exe'
    elif config_path(root).is_file():
        collection = root
        inspector = root / 'out/common/bin/cave_inspect.exe'
        if not inspector.is_file():
            # Bootstrap only the ROM-free native reader, never a game window.
            subprocess.run(['pwsh', '-NoProfile', '-File', str(root / 'scripts/Build.ps1'),
                            '-LauncherOnly', '-Jobs', '2'], check=True, stdout=sys.stderr)
    else:
        return root / 'game-data' / game
    command = [str(inspector), '--root', str(collection), '--game', game, '--full']
    report = json.loads(subprocess.check_output(command))['games'][0]
    if report['dataState'] != 'verified' or report['problems']:
        raise ValueError('Configured game data is incomplete or unsupported: ' + game)
    return Path(report['dataDirectory']).resolve(strict=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', required=True)
    parser.add_argument('--data-dir', type=Path)
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding='utf-8')
    print(build_data(args.game, args.data_dir))
