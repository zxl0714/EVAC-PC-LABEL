from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import sdoj_analyze
from project import digest
from test_xex_inventory import authored_xex


class SdojAnalysisTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='cave-sdoj-static-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.expected = dict(path='CA022100.bin', imageBase='0x88000000',
            entryPoint='0x88001000', titleId='435A07E7', sha256='a' * 64)
        self.report = dict(game='sdoj', module='CA022100.bin', toolMode=True,
            guestExecution=False, gpuInitialized=False, audioInitialized=False,
            inputInitialized=False, imageBase=0x88000000, entryPoint=0x88001000,
            titleId=0x435A07E7, imageBytes=4096, mainImageAllocationPreserved=True,
            staticLoadCycles=3, overlappingLoadRejectedSafely=True,
            importSummary=dict(functionalBehaviorVerified=False), sections=[])

    def validate(self, report=None):
        return sdoj_analyze.validate_census(report or self.report, self.expected, self.root, self.root)

    def test_bin_overlay_uses_normal_static_lifecycle_contract(self):
        result = self.validate()
        self.assertEqual(result['module'], 'CA022100.bin')
        self.assertFalse(result['importSummary']['functionalBehaviorVerified'])

    def test_another_title_entry_or_execution_is_rejected(self):
        for key, value in (('game', 'espgaluda2'), ('module', 'dat00.xex'),
                           ('entryPoint', 0x88001004), ('titleId', 0x435A07D8),
                           ('guestExecution', True), ('toolMode', False),
                           ('gpuInitialized', True), ('audioInitialized', True),
                           ('inputInitialized', True)):
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.validate(dict(self.report, **{key: value}))

    def test_bin_overlay_requires_overlap_isolation_and_three_loads(self):
        for key, value in (('mainImageAllocationPreserved', False),
                           ('staticLoadCycles', 1), ('overlappingLoadRejectedSafely', False)):
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.validate(dict(self.report, **{key: value}))

    def test_section_bytes_not_embedded_and_bad_ranges_rejected(self):
        (self.root / '0.section.bin').write_bytes(b'authored data')
        report = dict(self.report, sections=[dict(inImage=True, executable=True,
            bytes=13, dump='0.section.bin')])
        self.assertEqual(len(self.validate(report)['dumps'][0]['sha256']), 64)
        report['sections'][0]['bytes'] = 14
        with self.assertRaises(ValueError):
            self.validate(report)

    def authored_programs(self):
        checked = []
        for name in sorted(sdoj_analyze.PROGRAMS):
            data = authored_xex()
            struct.pack_into('>I', data, 76, 0x435A07E7)
            path = self.root / name.lower()
            path.write_bytes(data)
            checked.append(dict(path=path.name, bytes=len(data), sha256=digest(path)))
        return dict(variant='disc', checkedFiles=checked)

    def test_programs_include_both_bin_headers_with_canonical_names(self):
        identity = self.authored_programs()
        before = {p.name: p.read_bytes() for p in self.root.iterdir()}
        # Authored headers are not real supported programs: stub only the hash classifier.
        with patch.object(sdoj_analyze, 'identify', return_value=identity):
            result = sdoj_analyze.program_inputs(self.root)
        self.assertEqual([item['path'] for item in result['programs']],
                         ['default.xex', 'CA022100.bin', 'CA022110.bin'])
        self.assertEqual({item['titleId'] for item in result['programs']}, {'435A07E7'})
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.root.iterdir()})

    def test_unknown_programs_and_wrong_title_are_not_analyzed(self):
        with patch.object(sdoj_analyze, 'identify', return_value=dict(variant=None)):
            with self.assertRaisesRegex(ValueError, 'known SDOJ'):
                sdoj_analyze.program_inputs(self.root)
        identity = self.authored_programs()
        data = authored_xex()
        (self.root / 'default.xex').write_bytes(data)
        with patch.object(sdoj_analyze, 'identify', return_value=identity):
            with self.assertRaisesRegex(ValueError, 'execution identity'):
                sdoj_analyze.program_inputs(self.root)


if __name__ == '__main__':
    unittest.main()
