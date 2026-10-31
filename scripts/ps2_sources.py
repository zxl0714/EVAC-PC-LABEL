"""Restore pinned PS2 sources using the collection's shared dependency checks."""
import argparse
import json
from pathlib import Path

from ps4_sources import restore


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['restore', 'verify'])
    parser.add_argument('--cache-from', type=Path)
    args = parser.parse_args()
    print(json.dumps(restore(cache=args.cache_from, verify_only=args.action == 'verify',
                             backend='ps2'), indent=2))
