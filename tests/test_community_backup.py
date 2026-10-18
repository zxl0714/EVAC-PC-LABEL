from pathlib import Path
from contextlib import closing
import sqlite3
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from server.backup import backup


class CommunityBackupTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name).resolve()
        self.database = self.root / 'community.sqlite3'
        with closing(sqlite3.connect(self.database)) as db:
            db.execute('PRAGMA journal_mode=WAL')
            db.execute('CREATE TABLE fixture(value TEXT)')
            db.execute('INSERT INTO fixture VALUES(?)', ('authored fixture',))
            db.commit()

    def test_online_snapshot_has_integrity_and_preserves_source(self):
        target = backup(self.database, self.root / 'backups')
        with closing(sqlite3.connect(target)) as db:
            self.assertEqual(db.execute('PRAGMA integrity_check').fetchone(), ('ok',))
            self.assertEqual(db.execute('SELECT * FROM fixture').fetchall(), [('authored fixture',)])
        with closing(sqlite3.connect(self.database)) as db:
            self.assertEqual(db.execute('SELECT * FROM fixture').fetchall(), [('authored fixture',)])

    def test_missing_database_is_not_created(self):
        absent = self.root / 'absent.sqlite3'
        with self.assertRaises(FileNotFoundError):
            backup(absent, self.root / 'backups')
        self.assertFalse(absent.exists())
        self.assertFalse((self.root / 'backups').exists())

    def test_retains_seven_snapshots_and_unrelated_files(self):
        directory = self.root / 'backups'
        directory.mkdir()
        unrelated = directory / 'operator-note.txt'
        unrelated.write_text('keep', encoding='utf-8')
        targets = [backup(self.database, directory) for _ in range(8)]
        self.assertEqual(len(list(directory.glob('community-*.sqlite3'))), 7)
        self.assertFalse(targets[0].exists())
        self.assertTrue(targets[-1].exists())
        self.assertEqual(unrelated.read_text(encoding='utf-8'), 'keep')

    def test_linked_backup_directory_is_rejected(self):
        with patch.object(Path, 'is_symlink', return_value=True):
            with self.assertRaises(ValueError):
                backup(self.database, self.root / 'backups')


if __name__ == '__main__':
    unittest.main()
