import copy
import importlib.util
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/ps4'))
SPEC = importlib.util.spec_from_file_location('kt_binding_analysis', ROOT / 'scripts/ps4/inspect_bindings.py')
ANALYSIS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYSIS)


class BindingAnalysisTests(unittest.TestCase):
    def events(self):
        return [dict(event='image_prepared', image_name='eboot.bin', source_sha256='a' * 64, imports=1),
                dict(event='native_binding_census_import', image_name='eboot.bin', index=0, nid='fixture#A#A',
                     name='Fixture', library='FixtureLib', module='FixtureModule', type=2, status='unbound'),
                dict(event='native_binding_census_complete', images=1, imports=1, host_bound=0,
                     module_export_available=0, unbound=1, game_entry_reached=False,
                     module_initializers_called=False, capability_validation=False)]

    def test_summary_checks_terminal_counts_duplicates_and_no_execution(self):
        events = self.events()
        report = ANALYSIS.summarize(events)
        self.assertEqual(report['libraries'][0]['unbound'], 1)
        self.assertEqual(events, self.events())
        for mutate in (lambda e: e.pop(), lambda e: e.insert(1, dict(e[1])),
                       lambda e: e[-1].update(imports=2), lambda e: e[-1].update(game_entry_reached=True),
                       lambda e: e[0].update(imports=2), lambda e: e[1].update(status='made-up')):
            invalid = copy.deepcopy(events)
            mutate(invalid)
            with self.assertRaises(ValueError):
                ANALYSIS.summarize(invalid)

    def test_graph_uses_rip_relative_got_identity_and_retains_unreachable_unknowns(self):
        bindings = ANALYSIS.summarize(self.events())['bindings']
        functions = [dict(offset='0x10', name='Root', callees=[dict(offset='0x20')]),
                     dict(offset='0x20', name='DeliberatelyWrongName', callees=[]),
                     dict(offset='0x30', name='Unreachable', callees=[dict(offset='0x20')])]
        code = b'\xff\x25' + struct.pack('<i', 0x1000 - 0x20 - 6)
        report = ANALYSIS.annotate(bindings, dict(sha256='a' * 64, functions=functions),
                    lambda address, size: code if address == 0x20 else b'', {0x1000: 'fixture#A#A'}, ['0x10'])
        self.assertEqual(report['recognized_import_thunks'], 1)
        self.assertEqual(report['roots'][0]['imports'][0]['name'], 'Fixture')
        self.assertEqual(bindings[0]['static_direct_callers'], ['0x10', '0x30'])
        self.assertIn('callbacks', report['scope'])
        with self.assertRaises(ValueError):
            ANALYSIS.annotate(bindings, dict(sha256='a' * 64, functions=functions), lambda *_: b'', {}, ['0x99'])

    def test_wrong_got_identity_is_not_matched_by_friendly_name(self):
        bindings = ANALYSIS.summarize(self.events())['bindings']
        graph = dict(sha256='a' * 64, functions=[dict(offset='0x20', name='Fixture', callees=[])])
        code = b'\xff\x25' + struct.pack('<i', 0x1000 - 0x20 - 6)
        report = ANALYSIS.annotate(bindings, graph, lambda *_: code, {0x1000: 'other#A#A'}, ['0x20'])
        self.assertEqual(report['recognized_import_thunks'], 0)
        self.assertEqual(report['roots'][0]['imports'], [])
        self.assertEqual(bindings[0]['static_direct_callers'], [])

    def test_function_pointer_reachability_is_optional_and_not_called_execution(self):
        bindings = ANALYSIS.summarize(self.events())['bindings']
        graph = dict(sha256='a' * 64, functions=[
            dict(offset='0x10', name='Registrar', callees=[], function_references=[dict(offset='0x30')]),
            dict(offset='0x30', name='Callback', callees=[dict(offset='0x20')]),
            dict(offset='0x20', name='Thunk', callees=[])])
        code = b'\xff\x25' + struct.pack('<i', 0x1000 - 0x20 - 6)
        reader = lambda address, size: code if address == 0x20 else b''
        direct = ANALYSIS.annotate(bindings, graph, reader, {0x1000: 'fixture#A#A'}, ['0x10'])
        callbacks = ANALYSIS.annotate(bindings, graph, reader, {0x1000: 'fixture#A#A'}, ['0x10'], True)
        self.assertEqual(direct['roots'][0]['imports'], [])
        self.assertEqual(callbacks['roots'][0]['imports'][0]['name'], 'Fixture')
        self.assertTrue(callbacks['follows_function_references'])
        self.assertIn('do not prove execution', callbacks['scope'])

    def test_module_selection_does_not_confuse_equal_nids_between_images(self):
        main = ANALYSIS.summarize(self.events())['bindings'][0]
        module = dict(main, image_name='core.prx', name='CoreFixture', status='host_bound')
        graph = dict(sha256='a' * 64, functions=[
            dict(offset='0x10', name='CoreRoot', callees=[dict(offset='0x20')]),
            dict(offset='0x20', name='CoreThunk', callees=[])])
        code = b'\xff\x25' + struct.pack('<i', 0x1000 - 0x20 - 6)
        report = ANALYSIS.annotate([main, module], graph, lambda address, _: code if address == 0x20 else b'',
                                  {0x1000: 'fixture#A#A'}, ['0x10'], image_name='core.prx')
        self.assertEqual(report['image_name'], 'core.prx')
        self.assertEqual(report['roots'][0]['imports'][0]['name'], 'CoreFixture')
        self.assertEqual(report['roots'][0]['imports'][0]['status'], 'host_bound')
        self.assertNotIn('static_direct_callers', main)
        self.assertEqual(module['static_direct_callers'], ['0x10'])

    def test_callback_following_rejects_old_graph_without_reference_inventory(self):
        graph = dict(sha256='a' * 64, functions=[dict(offset='0x10', name='Root', callees=[])])
        with self.assertRaisesRegex(ValueError, 'function-reference inventory'):
            ANALYSIS.annotate([], graph, lambda *_: b'', {}, ['0x10'], True)
