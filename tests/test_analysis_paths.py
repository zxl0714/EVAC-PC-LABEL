from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import analysis_paths


class AnalysisPathTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name).resolve()
        self.patch = patch.object(analysis_paths, 'ROOT', self.root)
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def test_new_local_report_does_not_create_any_output(self):
        output = self.root / '.local/analysis/report.json'
        self.assertEqual(analysis_paths.private_output(output), output)
        self.assertFalse(output.parent.exists())

    def test_reports_must_be_new_and_below_local(self):
        for output in (self.root, self.root / '.local', self.root / 'report.json'):
            with self.subTest(output=output), self.assertRaises(ValueError):
                analysis_paths.private_output(output)
        output = self.root / '.local/existing.json'
        output.parent.mkdir()
        output.write_text('keep', encoding='utf-8')
        with self.assertRaises(FileExistsError):
            analysis_paths.private_output(output)
        self.assertEqual(output.read_text(encoding='utf-8'), 'keep')

    def test_explicit_inputs_protect_both_overlap_directions(self):
        source = self.root / '.local/analysis/source'
        for output in (source / 'report.json', source, source.parent):
            with self.subTest(output=output), self.assertRaises(ValueError):
                analysis_paths.private_output(output, source)
        output = source.parent / 'report.json'
        self.assertEqual(analysis_paths.private_output(output, source), output)

    def test_all_configured_data_roots_are_protected(self):
        (self.root / 'config.ini').write_text(
            '[game.unknown]\ndata_dir = .local/external-game\n', encoding='utf-8')
        for output in (self.root / '.local/external-game/report.json', self.root / '.local'):
            with self.subTest(output=output), self.assertRaises(ValueError):
                analysis_paths.private_output(output)
        output = self.root / '.local/reports/new.json'
        self.assertEqual(analysis_paths.private_output(output), output)

    def test_ambiguous_configured_data_roots_are_rejected(self):
        config = self.root / 'config.ini'
        for value in ('', 'C:relative', '\\relative', '\0'):
            config.write_text('[game.test]\ndata_dir = ' + value, encoding='utf-8')
            with self.subTest(value=value), self.assertRaises(ValueError):
                analysis_paths.private_output(self.root / '.local/report.json')

    def test_linked_output_is_rejected_before_resolving(self):
        with patch.object(Path, 'is_junction', return_value=True):
            with self.assertRaises(ValueError):
                analysis_paths.private_output(self.root / '.local/report.json')

    def test_census_children_use_fixed_relative_paths(self):
        directory = self.root / 'census'
        self.assertEqual(analysis_paths.analysis_child(directory, 'default/0.section.bin'),
                         directory / 'default/0.section.bin')
        for value in ('../outside', '/absolute', 'C:/absolute', 'a\\b', './data', 'a/../data'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                analysis_paths.analysis_child(directory, value)
        with patch.object(Path, 'is_junction', return_value=True):
            with self.assertRaises(ValueError):
                analysis_paths.analysis_child(directory, 'data')


if __name__ == '__main__':
    unittest.main()
