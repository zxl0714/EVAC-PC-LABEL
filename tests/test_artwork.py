import copy
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

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

    def test_catalogue_sources_and_reproducible_generation(self):
        entries = generate_artwork.artwork(self.root)
        self.assertEqual([entry['game'] for entry in entries],
                         ['futari', 'espgaluda2', 'ketsui', 'sdoj', 'daioujou', 'esprade', 'guwange', 'feveron'])
        destination = self.root / 'out'
        generate_artwork.generate(self.root, destination)
        before = {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in destination.iterdir()}
        generate_artwork.generate(self.root, destination)
        self.assertEqual(before, {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in destination.iterdir()})
        self.assertIn('std::array<Artwork,8>', (destination / 'artwork.h').read_text())

    def test_invalid_manifest(self):
        for key, value in [('resource', True), ('resource', 99), ('focusY', -1), ('focusY', 1.1),
                           ('sha256', '0' * 64), ('path', '../outside.jpg'), ('sourceUrl', 'http://example.org/a'),
                           ('copyright', ''), ('usage', 'bad\ntext'), ('fit', 'stretch')]:
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

    def test_branding_and_icon_resources_are_embedded(self):
        entries=generate_artwork.branding(self.root)
        self.assertEqual([item['name'] for item in entries],['logo','iconImage','icon'])
        destination=self.root/'brand-out'
        generate_artwork.generate(self.root,destination)
        self.assertIn('kBrandLogoResource=201',(destination/'artwork.h').read_text())
        self.assertIn('1 ICON ',(destination/'artwork.rc').read_text())
        self.assertIn('201 RCDATA ',(destination/'artwork.rc').read_text())
        resource=(destination/'artwork.rc').read_text(encoding='utf-8')
        for text in ('1 VERSIONINFO','"ProductName", "EVAC PC LABEL\\0"',
                     '"OriginalFilename", "EVAC PC LABEL.exe\\0"',
                     '"ProductVersion", "'+generate_artwork.VERSION+'\\0"'):
            self.assertIn(text,resource)

    def test_numeric_and_text_file_versions_follow_product_version(self):
        for version, numeric, text in [('0.6','0,6,0,0','0.6.0.0'),
                                       ('0.6.2','0,6,2,0','0.6.2.0'),
                                       ('1.2.3.4','1,2,3,4','1.2.3.4')]:
            with self.subTest(version=version), patch.object(generate_artwork,'VERSION',version):
                destination=self.root/'version-out'
                generate_artwork.generate(self.root,destination)
                resource=(destination/'artwork.rc').read_text(encoding='utf-8')
                self.assertIn('FILEVERSION '+numeric,resource)
                self.assertIn('PRODUCTVERSION '+numeric,resource)
                self.assertIn('"FileVersion", "'+text+'\\0"',resource)
                self.assertIn('"ProductVersion", "'+version+'\\0"',resource)
        for invalid in ('0','0.6-rc1','0.6.1.2.3','-1.0','0.65536'):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                generate_artwork.windows_version(invalid)

    def test_branding_rejects_non_alpha_png(self):
        path=self.root/'launcher/branding.json'
        record=json.loads(path.read_text(encoding='utf-8'))
        entry=record['assets'][0];image=self.root/entry['path']
        data=bytearray(image.read_bytes());data[25]=2;image.write_bytes(data)
        entry['sha256']=hashlib.sha256(data).hexdigest();path.write_text(json.dumps(record),encoding='utf-8')
        with self.assertRaisesRegex(ValueError,'RGBA'):
            generate_artwork.branding(self.root)

    def test_branding_rejects_corrupt_icon_even_with_updated_hash(self):
        path=self.root/'launcher/branding.json'
        original=json.loads(path.read_text(encoding='utf-8'))
        icon=self.root/original['assets'][2]['path'];before=icon.read_bytes()
        for contents in (b'MZnot-icon',before[:6],before+b'extra',before[:150]+b'bad frame'):
            with self.subTest(size=len(contents)):
                record=copy.deepcopy(original);icon.write_bytes(contents)
                record['assets'][2]['sha256']=hashlib.sha256(contents).hexdigest()
                path.write_text(json.dumps(record),encoding='utf-8')
                with self.assertRaises(ValueError):
                    generate_artwork.branding(self.root)

    def test_branding_rejects_path_and_identity_changes(self):
        path=self.root/'launcher/branding.json'
        original=json.loads(path.read_text(encoding='utf-8'))
        for update in ({'path':'launcher/assets/sdoj.jpg'},{'sha256':'0'*64},{'name':'unknown'}):
            record=copy.deepcopy(original);record['assets'][0].update(update)
            path.write_text(json.dumps(record),encoding='utf-8')
            with self.assertRaises(ValueError):
                generate_artwork.branding(self.root)


if __name__ == '__main__':
    unittest.main()
