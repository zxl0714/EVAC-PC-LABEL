import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import sdoj_inputs


class SdojInputTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='cave-sdoj-input-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.payload = {variant: {name: f'{variant}/{name}'.encode() for name in sdoj_inputs.PROGRAMS | sdoj_inputs.PATCHES}
                        for variant in ('disc', 'god')}
        self.update = b'synthetic title update container'
        self.reference = dict(format=1, game='sdoj', variants={})
        for variant in self.payload:
            self.reference['variants'][variant] = dict(
                programs={name: self.sha(self.payload[variant][name]) for name in sdoj_inputs.PROGRAMS},
                patches={name: self.sha(self.payload[variant][name]) for name in sdoj_inputs.PATCHES},
                updateContainers=[dict(bytes=len(self.update), sha256=self.sha(self.update))] if variant == 'disc' else [])

    @staticmethod
    def sha(data):
        return hashlib.sha256(data).hexdigest()

    def put(self, variant, names):
        for name in names:
            (self.root / name).write_bytes(self.payload[variant][name])

    def identify(self):
        before = {p.name: p.read_bytes() for p in self.root.iterdir() if p.is_file()}
        result = sdoj_inputs.identify(self.root, self.reference)
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.root.iterdir() if p.is_file()})
        self.assertFalse(result['readyToRun'])
        self.assertFalse(result['guestExecution'])
        self.assertFalse(result['sourceModified'])
        self.assertFalse(result['fullDataVerification'])
        return result

    def test_disc_and_named_independent_tu_container(self):
        self.put('disc', sdoj_inputs.PROGRAMS)
        (self.root / 'arbitrary-TU-name').write_bytes(self.update)
        result = self.identify()
        self.assertEqual(result['variant'], 'disc')
        self.assertEqual(result['state'], 'known_programs_and_update')
        self.assertEqual(result['variants']['disc']['updateContainers'], ['arbitrary-TU-name'])

    def test_god_requires_matching_three_payloads(self):
        self.put('god', sdoj_inputs.PROGRAMS | sdoj_inputs.PATCHES)
        result = self.identify()
        self.assertEqual((result['variant'], result['state']), ('god', 'known_programs_and_update'))
        (self.root / 'CA022100.binp').unlink()
        self.assertEqual(self.identify()['state'], 'missing_or_unrecognized_update')

    def test_mixed_release_update_is_not_accepted(self):
        self.put('disc', sdoj_inputs.PROGRAMS)
        self.put('god', sdoj_inputs.PATCHES)
        self.assertEqual(self.identify()['state'], 'mismatched_update')

    def test_missing_update_is_distinct_from_unknown_program(self):
        self.put('disc', sdoj_inputs.PROGRAMS)
        self.assertEqual(self.identify()['state'], 'missing_or_unrecognized_update')
        (self.root / 'default.xex').write_bytes(b'another version')
        result = self.identify()
        self.assertIsNone(result['variant'])
        self.assertEqual(result['state'], 'unknown_or_incomplete_programs')

    def test_archives_are_candidates_not_extracted_programs(self):
        (self.root / 'game.part01.rar').write_bytes(b'not executed or extracted')
        result = self.identify()
        self.assertEqual(result['state'], 'needs_extraction')
        self.assertEqual(result['filesHashed'], 0)
        self.assertEqual(result['containerCandidates'][0]['path'], 'game.part01.rar')

    def test_other_resources_are_not_hashed_or_recursed(self):
        self.put('disc', sdoj_inputs.PROGRAMS)
        (self.root / 'irrelevant-resource.dat').write_bytes(b'not a selected program')
        (self.root / 'res').mkdir()
        (self.root / 'res' / 'default.xex').write_bytes(b'do not guess nested game data roots')
        result = self.identify()
        self.assertEqual(result['filesHashed'], 3)
        self.assertNotIn('irrelevant-resource.dat', {p['path'] for p in result['checkedFiles']})

    def test_repository_reference_and_schema(self):
        reference = json.loads(sdoj_inputs.REFERENCE.read_text(encoding='utf-8'))
        self.assertEqual(sdoj_inputs.validate_reference(reference)['source']['commit'], 'cc35e038d4126bc1c359597dcb94e6815477818d')
        for mutate in (lambda x: x.update(format=True), lambda x: x['variants']['disc'].update(programs=[]),
                       lambda x: x['variants']['disc']['programs'].update({'../outside.xex': '0' * 64}),
                       lambda x: x['variants']['disc']['updateContainers'][0].update(bytes=True),
                       lambda x: x['variants']['god'].update(patches={'default.xexp': 'bad'})):
            with self.subTest(mutate=mutate):
                invalid = copy.deepcopy(reference)
                mutate(invalid)
                with self.assertRaises(ValueError):
                    sdoj_inputs.validate_reference(invalid)

    def test_cli_cannot_write_report_into_input(self):
        # This temporary input sits outside .local, so private_output also rejects it.
        result = subprocess.run([sys.executable, str(sdoj_inputs.ROOT / 'scripts/sdoj_inputs.py'), str(self.root),
                                 '--output', str(self.root / 'report.json')], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertFalse((self.root / 'report.json').exists())
        self.assertFalse(json.loads(result.stdout)['guestExecution'])


if __name__ == '__main__':
    unittest.main()
