"""Prepare optional, version-bound game read fixes into a new local output."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from analysis_paths import private_output

from game_fixes import prepare_music_fix


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-data', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--community-replay', action='store_true',
                        help='Include the development community replay-batch recovery overlay')
    parser.add_argument('--community-offline', action='store_true',
                        help='Include development offline round production; requires native -CommunityRounds and includes --community-replay')
    args = parser.parse_args()
    output = private_output(args.output, args.game_data)
    metadata = output.with_suffix('.json')
    private_output(metadata, args.game_data)
    if output.suffix != '.ktfix':
        raise ValueError('Fix output must use .ktfix')
    data, report = prepare_music_fix(args.game_data, community_replay=args.community_replay, community_offline=args.community_offline)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('xb') as target:
        target.write(data)
    with metadata.open('x', encoding='utf-8', newline='\n') as target:
        json.dump(report, target, indent=2)
        target.write('\n')
    print(json.dumps(dict(output=str(output), overlay_bytes=len(data), **report)))


if __name__ == '__main__':
    main()
