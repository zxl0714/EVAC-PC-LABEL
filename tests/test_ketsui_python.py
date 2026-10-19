"""ROM-free PS4 analysis and Ketsui read-overlay checks adapted to the collection.

Other imported native/device regressions stay in tests/ketsui for adaptation
with the PS4 build. They are not silently counted as passed source tests.
"""
import importlib.util
from pathlib import Path
import unittest


def load_tests(loader, tests, pattern):
    suite = unittest.TestSuite()
    for name in ('test_game_fixes', 'test_import_analysis', 'test_timing_analysis'):
        path = Path(__file__).parent / 'ketsui' / (name + '.py')
        spec = importlib.util.spec_from_file_location('collection_ketsui_' + name, path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        suite.addTests(loader.loadTestsFromModule(module))
    return suite
