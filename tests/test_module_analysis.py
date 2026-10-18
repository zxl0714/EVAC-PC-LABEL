import copy
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from analyze_modules import inspect_report


class ModuleAnalysisTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.output = Path(self.folder.name).resolve()
        self.target = self.output / 'default'
        self.target.mkdir()
        (self.target / '0.section.bin').write_bytes(b'authored test data')
        self.expected = dict(imageBase='0x82000000', titleId='12345678', sha256='a' * 64)
        self.report = dict(module='default.xex', toolMode=True, guestExecution=False,
            gpuInitialized=False, audioInitialized=False, inputInitialized=False,
            mainImageAllocationPreserved=True, staticLoadCycles=1,
            imageBase=0x82000000, titleId=0x12345678, imageBytes=4096,
            importSummary={'functionalBehaviorVerified':False},
            sections=[dict(inImage=True, executable=True, bytes=18, dump='0.section.bin')])

    def inspect(self, report=None, name='default.xex'):
        return inspect_report(report or self.report, name, self.expected, self.target, self.output)

    def test_records_metadata_not_section_bytes(self):
        result = self.inspect()
        self.assertEqual(result['module'], 'default.xex')
        self.assertEqual(result['inputSha256'], 'a' * 64)
        self.assertEqual(result['dumps'][0]['path'], 'default/0.section.bin')
        self.assertEqual(len(result['dumps'][0]['sha256']), 64)

    def test_execution_subsystems_and_wrong_identity_are_rejected(self):
        for key, value in (('toolMode',False), ('guestExecution',True), ('gpuInitialized',True),
                           ('audioInitialized',True), ('inputInitialized',True),
                           ('module','dat00.xex'), ('imageBase',0), ('titleId',0),
                           ('mainImageAllocationPreserved',False)):
            report = copy.deepcopy(self.report)
            report[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.inspect(report)

    def test_overlays_require_three_safe_static_cycles(self):
        report = dict(self.report, module='dat00.xex', staticLoadCycles=3,
                      overlappingLoadRejectedSafely=True)
        self.assertEqual(self.inspect(report, 'dat00.xex')['module'], 'dat00.xex')
        for key, value in (('staticLoadCycles',2), ('overlappingLoadRejectedSafely',False)):
            invalid = dict(report, **{key:value})
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.inspect(invalid, 'dat00.xex')

    def test_excluded_executable_truncated_duplicate_and_escaping_dumps_rejected(self):
        for section in (dict(inImage=False, executable=True),
                        dict(inImage=False, executable=False, dump='anything'),
                        dict(inImage=True, executable=True, bytes=19, dump='0.section.bin'),
                        dict(inImage=True, executable=True, bytes=18, dump='../0.section.bin')):
            report = dict(self.report, sections=[section])
            with self.subTest(section=section), self.assertRaises(ValueError):
                self.inspect(report)
        with self.assertRaises(ValueError):
            self.inspect(dict(self.report, sections=self.report['sections'] * 2))


if __name__ == '__main__':
    unittest.main()
