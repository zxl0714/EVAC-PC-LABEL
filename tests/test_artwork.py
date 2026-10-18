import copy
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import generate_artwork


class ArtworkTests(unittest.TestCase):
    def setUp(self):
        self.source = Path(__file__).resolve().parents[1]
        self.temporary = tempfile.TemporaryDirectory(prefix='cave-artwork-test-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        shutil.copytree(self.source / 'games', self.root / 'games')
        shutil.copytree(self.source / 'launcher', self.root / 'launcher')
        self.path = self.root / 'launcher/artwork.json'
        self.manifest = json.loads(self.path.read_text(encoding='utf-8'))

    def reject(self, change):
        value = copy.deepcopy(self.manifest)
        change(value)
        self.path.write_text(json.dumps(value), encoding='utf-8')
        with self.assertRaises((ValueError, KeyError, TypeError)):
            generate_artwork.artwork(self.root)

    def test_five_sources_and_reproducible_generation(self):
        entries = generate_artwork.artwork(self.root)
        self.assertEqual([entry['game'] for entry in entries],
                         ['futari', 'espgaluda2', 'ketsui', 'daioujou', 'esprade'])
        destination = self.root / 'out'
        generate_artwork.generate(self.root, destination)
        before = {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in destination.iterdir()}
        generate_artwork.generate(self.root, destination)
        self.assertEqual(before, {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in destination.iterdir()})
        self.assertIn('std::array<Artwork,5>', (destination / 'artwork.h').read_text())

    def test_invalid_manifest(self):
        for key, value in [('resource', True), ('resource', 99), ('focusY', -1), ('focusY', 1.1),
                           ('sha256', '0' * 64), ('path', '../outside.jpg'), ('sourceUrl', 'http://example.org/a'),
                           ('copyright', ''), ('usage', 'bad\ntext')]:
            with self.subTest(key=key, value=value):
                self.reject(lambda manifest: manifest['assets'][0].update({key: value}))

    def test_order_and_resource_identity(self):
        self.reject(lambda manifest: manifest['assets'].reverse())
        self.reject(lambda manifest: manifest['assets'][1].update(resource=101))

    def test_not_a_generic_binary_audit_exception(self):
        entry = self.manifest['assets'][0]
        image = self.root / entry['path']
        image.write_bytes(b'MZnot-an-image')
        entry['sha256'] = hashlib.sha256(image.read_bytes()).hexdigest()
        self.path.write_text(json.dumps(self.manifest), encoding='utf-8')
        with self.assertRaises(ValueError):
            generate_artwork.artwork(self.root)


if __name__ == '__main__':
    unittest.main()
