"""Check the authored oracle inputs without requiring ROMs or native builds."""
import unittest
import ps4_resource_fixtures as fixtures


class ResourcePreparationFixtures(unittest.TestCase):
    def test_authored_psb_covers_names_values_and_unicode(self):
        root = {'file_info': {'own.nut.m': [30, 40]}, '资源': ['hello', -2**48, True, False, None, 1.5]}
        self.assertEqual(fixtures.fixes.Psb(fixtures.psb(root)).root, root)

    def test_fixtures_are_unique_and_cover_all_operations(self):
        cases = fixtures.cases()
        self.assertEqual(len(cases), len({case['name'] for case in cases}))
        self.assertEqual({case['op'] for case in cases}, {'mask', 'unwrap', 'wrap', 'psb', 'fix', 'encode'})

    def test_expected_valid_and_rejection_cases(self):
        for case in fixtures.cases():
            with self.subTest(case=case['name']):
                reject = any(part in case['name'] for part in ('invalid', 'duplicate', 'changed', 'count-', 'short',
                    'truncated', 'wrong-', 'oversized', 'trailing', 'too-small', 'header', 'encode-path-'))
                if reject:
                    with self.assertRaises(Exception): fixtures.oracle(case)
                else:
                    fixtures.oracle(case)


if __name__ == '__main__': unittest.main()
