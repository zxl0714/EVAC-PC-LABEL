# Adapted from Mushihimesama-Futari-PC, server/backup.py.
"""Create a consistent online SQLite snapshot, retaining the newest seven."""
import argparse
from contextlib import closing
from datetime import datetime, timezone
from pathlib import Path
import sqlite3


def backup(database, directory):
    database, directory = Path(database).resolve(strict=True), Path(directory).absolute()
    linked = any(part.is_symlink() or getattr(part, 'is_junction', lambda: False)()
                 for part in (directory, *directory.parents))
    if not database.is_file() or linked:
        raise ValueError('Use a regular database and backup directory')
    directory.mkdir(mode=0o750, parents=True, exist_ok=True)
    name = datetime.now(timezone.utc).strftime('community-%Y%m%dT%H%M%S%f.sqlite3')
    target = directory / name
    # Exclusive creation protects prior snapshots even if the clock repeats.
    with target.open('xb'):
        pass
    with closing(sqlite3.connect(database.as_uri() + '?mode=ro', uri=True)) as source, \
            closing(sqlite3.connect(str(target))) as snapshot:
        source.backup(snapshot)
        if snapshot.execute('PRAGMA integrity_check').fetchone() != ('ok',):
            raise RuntimeError('Backup integrity check failed')
    for old in sorted(directory.glob('community-*.sqlite3'), reverse=True)[7:]:
        if old.parent.resolve() != directory.resolve() or old.is_symlink():
            raise RuntimeError('Unexpected backup path')
        old.unlink()
    return target


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database', type=Path, required=True)
    parser.add_argument('--directory', type=Path, required=True)
    args = parser.parse_args()
    print(backup(args.database, args.directory).name)
