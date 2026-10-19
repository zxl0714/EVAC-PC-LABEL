"""Bounded native envelope inspection with entirely authored, non-playable data."""
import gzip
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'out/native/KetsuiNativeProbe.exe'
RECORD_SIZE, DATA_OFFSET, DATA_SIZE = 0x80000, 0x500, 0x78000
SNAPSHOT_LIMIT = 2 * 1024 * 1024


def chunk(name=b'AUTHORED', payload=b'non-playable fixture'):
    return bytes([len(name)]) + name + struct.pack('<I', len(payload)) + payload


def record(snapshot=None):
    snapshot = zlib.compress(chunk() + chunk(b'SECOND', b'fixture 2')) if snapshot is None else snapshot
    result = bytearray(RECORD_SIZE)
    struct.pack_into('<10I', result, 0, 0x10001, 0x10001, 0, 0, 1024, 600, len(snapshot), 6, 0x1fff, 0)
    result[48:55] = b'fixture'
    result[80:92] = b'ARCADE_01000'
    struct.pack_into('<IHH', result, 108, 9001, 1, 2)
    result[120:130] = b'2000/01/01'
    result[DATA_OFFSET:DATA_OFFSET + len(snapshot)] = snapshot
    return result


def key_record(edges, *, total=1024, consumed=600, players=1, mask=1):
    result = record()
    struct.pack_into('<II', result, 16, total, consumed)
    struct.pack_into('<III', result, 28, players, mask, len(edges))
    snapshot_size, = struct.unpack_from('<I', result, 24)
    directory = DATA_OFFSET + snapshot_size
    stride = total // 512
    words = directory + players * mask.bit_count() * stride
    for cursor, (page, line, word) in enumerate(sorted(edges)):
        result[directory + line * stride + page // 8] |= 1 << (page % 8)
        struct.pack_into('<Q', result, words + cursor * 8, word)
    return result


@unittest.skipUnless(EXE.exists(), 'Build-Native.ps1 required')
class ReplayEnvelopeTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix='kt-replay-envelope-')
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.counter = 0

    def inspect(self, payload, valid=False, *, board=None, score=9001):
        self.counter += 1
        source = self.root / f'自造 录像 {self.counter}.attachment'
        source.write_bytes(payload)
        before = source.read_bytes()
        arguments = [] if board is None else [str(board), str(score)]
        result = subprocess.run([str(EXE), '--community-replay-check', str(source), *arguments], cwd=self.root,
                                capture_output=True, text=True, encoding='utf-8', timeout=10,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.assertEqual(result.returncode, 0 if valid else 2, result.stdout + result.stderr)
        self.assertEqual(result.stderr, '')
        event, = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(event['event'], 'community_replay_envelope_check')
        self.assertEqual(event['valid_envelope'], valid)
        self.assertFalse(event['native_playback_validated'])
        self.assertEqual(source.read_bytes(), before)
        self.assertEqual(set(self.root.iterdir()), {self.root / f'自造 录像 {i}.attachment'
                                                   for i in range(1, self.counter + 1)})
        return event

    def test_authored_two_layer_envelope_exact_size_fields_and_no_playback_claim(self):
        raw = record()
        event = self.inspect(zlib.compress(raw), valid=True)
        self.assertEqual(event['record_bytes'], RECORD_SIZE)
        self.assertEqual(event['record_sha256'], hashlib.sha256(raw).hexdigest())
        self.assertEqual((event['version'], event['data_version']), (0x10001, 0x10001))
        self.assertEqual((event['total_frames'], event['consumed_frames']), (1024, 600))
        self.assertEqual((event['snapshot_bytes'], event['snapshot_chunks']),
                         (len(chunk() + chunk(b'SECOND', b'fixture 2')), 2))
        self.assertEqual((event['score'], event['character'], event['stage']), (9001, 1, 2))
        self.assertEqual(event['region'], 'ARCADE_01000')
        self.assertEqual((event['kind'], event['mode'], event['metadata_matched']), (0, 0, False))
        self.assertEqual(event['scope'], 'envelope_snapshot_framing_and_key_extents')
        self.assertEqual(event['key_pages'], 0)
        self.assertEqual(event['key_directory_bytes'], 2 * 6 * 13)

    def test_selected_score_mode_family_and_ship_category_match_for_all_supported_boards(self):
        for group, mode in ((0, 0), (1, 0), (2, 0), (3, 1), (4, 2), (5, 4)):
            for ship in range(3):
                with self.subTest(group=group, ship=ship):
                    raw = record()
                    struct.pack_into('<H', raw, 106, mode)
                    struct.pack_into('<H', raw, 112, 5 if ship == 2 else 1)
                    event = self.inspect(zlib.compress(raw), valid=True, board=group * 10 + ship)
                    self.assertTrue(event['metadata_matched'])
                    self.assertEqual(event['mode'], mode)

    def test_selected_score_mismatch_and_64bit_expectations_are_not_truncated(self):
        for score in (0, 9000, 9002, 9001 + 2 ** 32, 2 ** 63 - 1):
            with self.subTest(score=score):
                self.inspect(zlib.compress(record()), board=0, score=score)

    def test_backup_kind_and_game_submode_are_separate_fields(self):
        for offset, value in ((104, 1), (104, 2), (104, 65535), (106, 1), (106, 2), (106, 3), (106, 4), (106, 65535)):
            with self.subTest(offset=offset, value=value):
                raw = record()
                struct.pack_into('<H', raw, offset, value)
                self.inspect(zlib.compress(raw), board=0)

    def test_ship_colors_are_grouped_by_four_without_guessing_the_board_from_color(self):
        for ship in range(2):
            for color in range(4):
                raw = record()
                struct.pack_into('<H', raw, 112, ship * 4 + color)
                with self.subTest(ship=ship, color=color):
                    self.inspect(zlib.compress(raw), valid=True, board=ship + 1)
                    self.inspect(zlib.compress(raw), board=2 - ship)
        for character in (8, 65535):
            raw = record()
            struct.pack_into('<H', raw, 112, character)
            self.inspect(zlib.compress(raw), board=0)

    def test_foreign_core_custom_and_unmapped_boards_do_not_get_compatibility_guesses(self):
        for board in (3, 9, 13, 60, 61, 62, 99, 2 ** 32 - 1):
            with self.subTest(board=board):
                self.inspect(zlib.compress(record()), board=board)
        raw = record()
        raw[80:92] = b'ARCADE_01001'
        self.inspect(zlib.compress(raw), valid=True)  # Generic structural mode is still distinct.
        self.inspect(zlib.compress(raw), board=0)

    def test_outer_checksum_truncation_trailing_concatenated_gzip_raw_and_dictionary_rejected(self):
        raw = record()
        compressed = zlib.compress(raw)
        corrupt = compressed[:-1] + bytes([compressed[-1] ^ 1])
        compressor = zlib.compressobj(zdict=b'authored dictionary')
        dictionary = compressor.compress(raw) + compressor.flush()
        for payload in (b'', b'not a compressed stream', compressed[:-1], corrupt,
                        compressed + b'x', compressed + compressed, gzip.compress(raw),
                        zlib.compress(raw, wbits=-15), dictionary):
            with self.subTest(size=len(payload)):
                self.inspect(payload)

    def test_outer_exact_size_and_expansion_limit(self):
        for raw in (record()[:-1], record() + b'\0', bytes(16 * 1024 * 1024)):
            with self.subTest(size=len(raw)):
                self.inspect(zlib.compress(raw))

    def test_input_byte_limit(self):
        self.inspect(b'x' * (8 * 1024 * 1024 + 1))

    def test_incompatible_versions_and_invalid_header_extents(self):
        cases = ((0, 0), (0, 0x10002), (4, 0x10002), (8, DATA_SIZE + 1), (12, 1),
                 (16, 0), (16, 1023), (20, 0), (20, 1025), (24, 0), (24, DATA_SIZE + 1),
                 (28, 0), (28, 9), (32, 0), (36, 65))
        for offset, value in cases:
            with self.subTest(offset=offset, value=value):
                raw = record()
                struct.pack_into('<I', raw, offset, value)
                self.inspect(zlib.compress(raw))

    def test_unterminated_metadata_empty_title_and_invalid_region(self):
        for offset, size, payload in ((48, 16, b'x' * 16), (48, 16, bytes(16)),
                                      (80, 16, b'x' * 16), (80, 16, bytes(16)),
                                      (80, 16, b'../evil\0' + bytes(8)),
                                      (120, 17, b'x' * 17), (137, 17, b'x' * 17)):
            with self.subTest(offset=offset, payload=payload):
                raw = record()
                raw[offset:offset + size] = payload
                self.inspect(zlib.compress(raw))

    def test_inner_stream_is_complete_zlib_and_bounded_before_chunk_parsing(self):
        original = zlib.compress(chunk())
        compressor = zlib.compressobj(zdict=b'inner fixture')
        dictionary = compressor.compress(chunk()) + compressor.flush()
        for snapshot in (b'bad', original[:-1], original + b'x', original + original,
                         gzip.compress(chunk()), zlib.compress(chunk(), wbits=-15), dictionary,
                         zlib.compress(bytes(SNAPSHOT_LIMIT + 1))):
            with self.subTest(size=len(snapshot)):
                self.inspect(zlib.compress(record(snapshot)))

    def test_chunk_header_length_payload_extent_name_uniqueness_and_count(self):
        malformed = (b'', b'\0', b'\x08abc',
                     b'\x01x' + struct.pack('<I', 0xffffffff),
                     chunk(b'\0'), chunk(b'\xff'), chunk() + chunk(),
                     b''.join(chunk(f'id{i:04}'.encode(), b'') for i in range(4097)))
        for state in malformed:
            with self.subTest(size=len(state)):
                self.inspect(zlib.compress(record(zlib.compress(state))))

    def test_missing_input_returns_structured_failure_without_creating_profile(self):
        result = subprocess.run([str(EXE), '--community-replay-check', str(self.root / 'missing')],
                                cwd=self.root, capture_output=True, text=True, encoding='utf-8', timeout=10,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.assertEqual(result.returncode, 2)
        self.assertFalse(json.loads(result.stdout)['valid_envelope'])
        self.assertEqual(list(self.root.iterdir()), [])

    def test_exact_snapshot_output_limit_and_maximum_name_length_are_accepted(self):
        name = b'n' * 255
        size = SNAPSHOT_LIMIT - 1 - len(name) - 4
        snapshot = zlib.compress(chunk(name, bytes(size)))
        event = self.inspect(zlib.compress(record(snapshot)), valid=True)
        self.assertEqual(event['snapshot_bytes'], SNAPSHOT_LIMIT)
        self.assertEqual(event['snapshot_chunks'], 1)

    def test_exact_chunk_count_limit_does_not_reject_the_last_valid_chunk(self):
        state = b''.join(chunk(f'id{i:04}'.encode(), b'') for i in range(4096))
        event = self.inspect(zlib.compress(record(zlib.compress(state))), valid=True)
        self.assertEqual(event['snapshot_chunks'], 4096)

    def test_more_than_64_key_pages_are_valid_when_directory_and_capacity_match(self):
        raw = key_record([(page, 0, 1) for page in range(128)], total=8192, consumed=8192)
        event = self.inspect(zlib.compress(raw), valid=True)
        self.assertEqual(event['key_pages'], 128)
        self.assertEqual(event['key_directory_bytes'], 16)
        self.assertGreater(event['key_page_capacity'], 128)

    def test_key_pages_follow_time_then_player_bit_and_allow_full_width_mask(self):
        raw = key_record([(0, 3, 0x8000000000000001), (1, 0, 1), (1, 2, 1)],
                         total=512, consumed=65, players=2, mask=0x80000001)
        event = self.inspect(zlib.compress(raw), valid=True)
        self.assertEqual(event['key_pages'], 3)
        self.assertEqual(event['key_directory_bytes'], 4)

    def test_last_full_or_partial_page_edge_bounds(self):
        for consumed, edges in ((64, [(0, 0, 0xffffffffffffffff)]),
                                (65, [(0, 0, 1), (1, 0, 1)])):
            with self.subTest(consumed=consumed):
                self.inspect(zlib.compress(key_record(edges, total=512, consumed=consumed)), valid=True)
        for edges in ([(0, 0, 0)], [(1, 0, 2)], [(2, 0, 1)]):
            with self.subTest(edges=edges):
                self.inspect(zlib.compress(key_record(edges, total=512, consumed=65)))

    def test_key_population_and_capacity_are_not_trusted_from_header(self):
        raw = key_record([(0, 0, 1)])
        for declared in (0, 2, 0xffffffff):
            with self.subTest(declared=declared):
                bad = bytearray(raw)
                struct.pack_into('<I', bad, 36, declared)
                self.inspect(zlib.compress(bad))
        bad = key_record([])
        struct.pack_into('<I', bad, 24, DATA_SIZE - 1)
        self.inspect(zlib.compress(bad))  # Directory cannot fit after the declared snapshot.

    def test_huge_logical_frame_directory_rejected_before_iteration_or_allocation(self):
        raw = record()
        struct.pack_into('<I', raw, 16, 0xfffffe00)
        struct.pack_into('<II', raw, 28, 8, 0xffffffff)
        self.inspect(zlib.compress(raw))

    def test_zero_pages_and_bounded_dense_directory_are_valid_without_false_positives(self):
        raw = key_record([], total=512000, consumed=1, players=8, mask=0xffffffff)
        event = self.inspect(zlib.compress(raw), valid=True)
        self.assertEqual(event['key_pages'], 0)
        self.assertEqual(event['key_directory_bytes'], 256000)

    def test_zlib_dependency_is_pinned_licensed_hashed_and_static_memory_only(self):
        dependencies = json.loads((ROOT / 'third_party/community/dependencies.json').read_text())
        lock, = [item for item in dependencies if item['name'] == 'zlib']
        self.assertEqual(lock['revision'], 'da607da739fa6047df13e66a2af6b8bec7c2a498')
        self.assertEqual(lock['url'], 'https://github.com/madler/zlib.git')
        license = ROOT / '.deps/zlib/LICENSE'
        if license.exists():
            self.assertEqual((ROOT / 'LICENSES/zlib-LICENSE.txt').read_text().strip(), license.read_text().strip())
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertIn('add_library(KetsuiZlib STATIC', cmake)
        self.assertIn('target_compile_definitions(KetsuiZlib PUBLIC Z_PREFIX)', cmake)
        self.assertNotIn('.deps/zlib/gz', cmake)
        self.assertIn("'.deps/zlib/' + $_", (ROOT / 'scripts/Build-Native.ps1').read_text())


if __name__ == '__main__':
    unittest.main()
