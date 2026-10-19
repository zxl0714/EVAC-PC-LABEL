"""Check the authored oracle inputs without requiring ROMs or native builds."""
import unittest
import ps4_resource_fixtures as fixtures


class ResourcePreparationFixtures(unittest.TestCase):
    def test_cached_replay_uses_stored_data_without_changing_global_np_or_validation(self):
        source=fixtures.authored_local_replay()
        fixed=fixtures.fix_local_replay(source)
        self.assertTrue(fixed.startswith(b'// authored outside prefix\n'))
        self.assertTrue(fixed.endswith(b'// authored outside suffix\n'))
        self.assertIn(b'function is_blocked_id(_id_h,_id_l) { return authored_existing_list; }',fixed)
        self.assertIn(b'if (_ok && load_replaydata(m_title_ctrl, _n))',fixed)
        self.assertIn(b'local _psnid = authored_stored_name;',fixed)
        self.assertIn(b'local accountID = false;',fixed)
        self.assertNotIn(b'ugc_error_dialog()',fixed)
        self.assertNotIn(b'NetworkModule(null)',fixed)
        self.assertNotIn(b'initializeIDMapper()',fixed)
        self.assertEqual(fixed.count(b'{'),fixed.count(b'}'))
        with self.assertRaises(ValueError):fixtures.fix_local_replay(fixed)

    def test_peer_fallback_is_scoped_and_preserves_existing_list_path(self):
        source=(b'// outside\nfunction is_blocked_id(_id_h,_id_l) {\n'
                b'  if (_net.initializeIDMapper() < 0) return true;\n'
                b'  return authored_existing_list_result;\n}\n'
                b'function getLocalEntryMax() { return 10; }\n')
        fixed=fixtures.fix_replay_peer(source)
        self.assertTrue(fixed.startswith(b'// outside\n'))
        self.assertTrue(fixed.endswith(b'function getLocalEntryMax() { return 10; }\n'))
        self.assertIn(b'if (::g_ranking_manager && ::g_ranking_manager.isInitialized()) return false;',fixed)
        self.assertIn(b'    return true;\n  }\n  return authored_existing_list_result;',fixed)
        with self.assertRaises(ValueError):fixtures.fix_replay_peer(fixed)

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
