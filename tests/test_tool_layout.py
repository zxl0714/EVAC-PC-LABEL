"""Operational tool manifests are independent of historical game imports."""
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import sdk_sources


class ToolLayoutTests(unittest.TestCase):
    def test_active_dependency_lock_contains_only_current_pins(self):
        lock = json.loads((ROOT / 'tools/sdk-source.lock.json').read_text(encoding='utf-8'))
        self.assertEqual(set(lock), {'format', 'submodules'})
        self.assertEqual(lock['format'], 1)
        self.assertEqual(len(sdk_sources.locked_modules(ROOT)), 22)
        for name in ('sdk-source.lock.json', 'packages.lock.json'):
            text = (ROOT / 'tools' / name).read_text(encoding='utf-8')
            self.assertNotIn('ESPGaluda2-PC', text)
            self.assertNotIn('Mushihimesama-Futari-PC', text)
            self.assertNotIn('Ketsui-PC', text)

    def test_import_origins_and_snapshot_remain_in_documentation(self):
        self.assertFalse((ROOT / 'tools/references.json').exists())
        origins = json.loads((ROOT / 'docs/SOURCE_ORIGINS.json').read_text(encoding='utf-8'))
        self.assertTrue(origins['imports'])
        baseline = json.loads((ROOT / 'docs/provenance/sdk-import-baseline.json').read_text(encoding='utf-8'))
        self.assertEqual(baseline['referenceCommit'], '7dced5979076329db71b1296c81944c83fb72b48')
        self.assertGreater(len(baseline['files']), 1000)
        self.assertEqual(baseline['submodules'], sdk_sources.locked_modules(ROOT))


if __name__ == '__main__':
    unittest.main()
