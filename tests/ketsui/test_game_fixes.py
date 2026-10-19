"""Read-overlay preparation tests use authored text and bytes, not game resources."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'scripts/ketsui'))
from game_fixes import (MAGIC, Psb, archive_key, digest, encode_read_patches,
                        fix_direct_menu, fix_ranking_replay_batch, fix_community_local_round_gate, unwrap, wrap_fixed)


class GameFixTests(unittest.TestCase):
    def entry(self, **overrides):
        result = dict(path='body.bin', file_size=100, offset=30,
                      expected_sha256=digest(b'original'), replacement=b'authored')
        result.update(overrides)
        return result

    def test_mdf_roundtrip_zero_padding_and_original_buffer_unchanged(self):
        raw = b'authored script\n' * 100
        before = bytes(raw)
        packed = wrap_fixed(raw, b'authored seed', 'own.nut.m', 300)
        self.assertEqual(len(packed), 300)
        self.assertEqual(unwrap(packed, b'authored seed', 'own.nut.m'), raw)
        self.assertEqual(raw, before)

    def test_mdf_extent_overflow_bad_key_corruption_and_header_rejected(self):
        with self.assertRaises(ValueError):
            wrap_fixed(b'cannot fit', b'key', 'test', 8)
        packed = wrap_fixed(b'hello', b'key', 'test', 64)
        for value in (b'mdf\0', packed[:4] + struct.pack('<I', 33 * 1024 * 1024) + packed[8:],
                      packed[:-1] + bytes([packed[-1] ^ 1])):
            with self.subTest(data=value[:8]), self.assertRaises((ValueError, zlib.error)):
                unwrap(value, b'key', 'test')
        with self.assertRaises((ValueError, zlib.error)):
            unwrap(packed, b'wrong key', 'test')

    def test_music_transform_changes_only_two_confirmations_and_preserves_cancel_code(self):
        anchor = b'm_bgm_mode[_gamemode] = _bgmMode;'
        raw = b'// own comment\n' + anchor + b'\nif (cancel) { keep_old(); }\n' + anchor + b'\n'
        result = fix_direct_menu(raw)
        self.assertEqual(result.count(b'CheckValueChangeAutosave'), 2)
        self.assertEqual(result.count(b'this._check_autosave();'), 2)
        self.assertIn(b'if (cancel) { keep_old(); }', result)
        self.assertNotIn(b'// own comment', result)
        self.assertEqual(result.count(anchor), 2)

    def test_music_transform_requires_both_known_paths_and_preserves_crlf(self):
        anchor = b'm_bgm_mode[_gamemode] = _bgmMode;'
        for count in (0, 1, 3):
            with self.assertRaises(ValueError):
                fix_direct_menu((anchor + b'\n') * count)
        result = fix_direct_menu((anchor + b'\r\n') * 2)
        self.assertNotIn(b'\n', result.replace(b'\r\n', b''))

    def authored_ranking_batch(self, newline=b'\n'):
        return newline.join((
            b'function before() { return -73; }',
            b'function RankingManager::_recordEntryBody(_boardIdArray, _entryInfo, _entryId)',
            b'{',
            b'  local result = this._recordScore(_boardIdArray, _entryInfo);',
            b'  if (typeof result == "array") {',
            b'    result = 0;',
            b'    foreach (board in _boardIdArray) {',
            b'          result = this._recordScoreData(board, _entryInfo);',
            b'          if ( !this.isSucceeded(result) ) ',
            b'            break;',
            b'    }',
            b'  }',
            b'  return result;',
            b'}',
            b'function friend_ranking_sort_func(a, b) { return a <=> b; }',
            b'function after() { return 91; }', b''))

    def test_replay_batch_attempts_remaining_boards_but_preserves_first_error(self):
        for newline in (b'\n', b'\r\n'):
            with self.subTest(newline=newline):
                raw = self.authored_ranking_batch(newline)
                result = fix_ranking_replay_batch(raw)
                self.assertNotIn(b'break;', result)
                self.assertIn(b'  local first_transfer_error = null;', result)
                self.assertIn(b'!this.isSucceeded(result) && first_transfer_error == null', result)
                self.assertIn(b'first_transfer_error = result;', result)
                self.assertIn(b'return first_transfer_error != null ? first_transfer_error : result;', result)
                for unchanged in (b'function before() { return -73; }',
                                  b'result = this._recordScoreData(board, _entryInfo);',
                                  b'function friend_ranking_sort_func(a, b) { return a <=> b; }',
                                  b'function after() { return 91; }'):
                    self.assertIn(unchanged, result)
                self.assertNotIn(b'\n', result.replace(newline, b''))

    def test_replay_batch_rejects_changed_or_ambiguous_control_flow(self):
        raw = self.authored_ranking_batch()
        for changed in (b'', raw + raw,
                        raw.replace(b'RankingManager::_recordEntryBody', b'other_body'),
                        raw.replace(b'function friend_ranking_sort_func(', b'function other_sort('),
                        raw.replace(b'            break;', b'            return result;'),
                        raw.replace(b'  local result = this._recordScore(_boardIdArray, _entryInfo);', b'  local result = 0;'),
                        raw.replace(b'  return result;', b'  return 0;')):
            with self.subTest(changed=changed[:60]), self.assertRaises(ValueError):
                fix_ranking_replay_batch(changed)

    def test_overlay_binary_has_scope_source_and_payload_hashes(self):
        entry = self.entry()
        result = encode_read_patches('CUSA00001', '01.02', [entry])
        self.assertEqual(result[:8], MAGIC)
        self.assertEqual(struct.unpack_from('<4I', result, 8), (1, 9, 5, 0))
        self.assertEqual(result[24:38], b'CUSA0000101.02')
        self.assertEqual(result[62:94], bytes.fromhex(entry['expected_sha256']))
        self.assertEqual(result[94:126], hashlib.sha256(entry['replacement']).digest())
        self.assertTrue(result.endswith(b'body.binauthored'))

    def authored_post_game(self, newline=b'\n'):
        return newline.join((b'function unrelated_query() { return ranking_is_network_ok_with_sign_in(); }',
            b'function unrelated_restriction() { return !_net.getUgcRestricted(); }',
            b'  function checkRankingAndRegistRanking() {',
            b'    if (checkRegistRanking()) {',
            b'      if (ranking_is_network_ok_with_sign_in() && !_net.getUgcRestricted()) {',
            b'        initRanking(this, true, this.TYPE_MODE_ARRAY_DEFAULT[GameMode.ETC + _round_type]);',
            b'        local _ret = ::g_ranking_ctrl.registRankingAttackHighscore(m_replay_score, _ship);',
            b'        initRanking(this, true, this.TYPE_MODE_ARRAY_DEFAULT[_gamemode]);',
            b'        local _ret = ::g_ranking_ctrl.registRankingAttackHighscore(m_replay_score, _ship);',
            b'        return _ret;', b'      }', b'    }', b'  }',
            b'  function checkRegistRanking() { return record_mode && eligible; }', b''))

    def test_local_round_gate_uses_local_backend_only_for_eligible_producer_and_keeps_other_policies(self):
        for newline in (b'\n', b'\r\n'):
            with self.subTest(newline=newline):
                raw = self.authored_post_game(newline)
                fixed = fix_community_local_round_gate(raw)
                expected = raw.replace(b'ranking_is_network_ok_with_sign_in() && !_net.getUgcRestricted()',
                                       b'::g_ranking_manager.isInitialized()')
                for mode in (b'GameMode.ETC + _round_type', b'_gamemode'):
                    expected = expected.replace(b'this.TYPE_MODE_ARRAY_DEFAULT[' + mode + b']);',
                                                b'this.TYPE_MODE_ARRAY_DEFAULT[' + mode + b'], LEADERBOARD_PLAYER_FILTER_MINE, true);')
                self.assertEqual(fixed, expected)
                self.assertIn(b'function unrelated_query() { return ranking_is_network_ok_with_sign_in(); }', fixed)
                self.assertIn(b'function unrelated_restriction() { return !_net.getUgcRestricted(); }', fixed)
                self.assertIn(b'if (checkRegistRanking()) {', fixed)
                self.assertIn(b'return _ret;', fixed)
                self.assertIn(b'return record_mode && eligible;', fixed)

    def test_local_round_gate_compacts_only_whole_line_comments_inside_changed_method(self):
        raw = b'// keep outside\r\n' + self.authored_post_game(b'\r\n').replace(
            b'    if (checkRegistRanking()) {',
            b'    // removable comment\r\n    if (checkRegistRanking()) { // preserve inline')
        fixed = fix_community_local_round_gate(raw)
        self.assertTrue(fixed.startswith(b'// keep outside\r\n'))
        self.assertNotIn(b'removable comment', fixed)
        self.assertIn(b'if (checkRegistRanking()) { // preserve inline\r\n', fixed)

    def test_local_round_gate_rejects_ambiguous_or_changed_producer_boundaries(self):
        raw = self.authored_post_game()
        for changed in (b'', raw + raw, raw.replace(b'checkRankingAndRegistRanking', b'other_producer'),
                        raw.replace(b'  function checkRegistRanking()', b'  function other_check()'),
                        raw.replace(b'if (checkRegistRanking()) {', b'if (true) {'),
                        raw.replace(b'initRanking(this, true, this.TYPE_MODE_ARRAY_DEFAULT[_gamemode]);', b'initRanking(other);'),
                        raw.replace(b'ranking_is_network_ok_with_sign_in() && ', b''),
                        raw.replace(b'local _ret = ::g_ranking_ctrl.registRankingAttackHighscore', b'local _ret = other_submit')):
            with self.subTest(changed=changed[:70]), self.assertRaises(ValueError):
                fix_community_local_round_gate(changed)

    def test_overlay_invalid_identity_and_paths_rejected(self):
        for title, version in (('CUSA0001', '01.02'), ('CUSA00001', '1.02'), ('CUSA00001', '01/02')):
            with self.assertRaises(ValueError):
                encode_read_patches(title, version, [self.entry()])
        for path in ('', '../body', '/body', 'a//b', 'a/./b', 'a/../b', 'a\\b', 'C:body', 'a\0b'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                encode_read_patches('CUSA00001', '01.02', [self.entry(path=path)])

    def test_overlay_invalid_ranges_overlaps_and_inconsistent_file_sizes_rejected(self):
        for change in (dict(file_size=-1), dict(offset=-1), dict(offset=100), dict(file_size=1 << 63),
                       dict(replacement=b''), dict(replacement=bytes(262145)), dict(expected_sha256='00')):
            with self.subTest(change=list(change)), self.assertRaises(ValueError):
                encode_read_patches('CUSA00001', '01.02', [self.entry(**change)])
        for entries in ([], [self.entry()] * 17, [self.entry(), self.entry(offset=31)],
                        [self.entry(), self.entry(offset=50, file_size=101)]):
            with self.assertRaises(ValueError):
                encode_read_patches('CUSA00001', '01.02', entries)

    def test_overlay_unsorted_nonoverlapping_ranges_are_canonicalized(self):
        entries = [self.entry(offset=50), self.entry(offset=30)]
        self.assertEqual(encode_read_patches('CUSA00001', '01.02', entries),
                         encode_read_patches('CUSA00001', '01.02', entries[::-1]))

    def test_original_executable_and_psb_identity_are_required(self):
        with self.assertRaises(ValueError):
            archive_key(b'authored non executable')
        for raw in (b'', bytes(40), b'PSB\0\2\0\0\0' + bytes(32)):
            with self.assertRaises((ValueError, IndexError)):
                Psb(raw)


if __name__ == '__main__':
    unittest.main()
