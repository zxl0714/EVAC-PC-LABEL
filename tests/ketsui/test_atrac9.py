"""Authored RIFF/ATRAC9 fixtures only; no game assets or audio samples."""
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'out/native/KetsuiNativeProbe.exe'


def authored_wave(*, looping=False):
    # Mono frame: normal first header, minimum bands, CLC zero scale factors,
    # zero spectra. The remaining bytes are superframe padding.
    block = bytearray(64)
    block[4] = 4
    encoded = block
    config = 0xe00770fe
    channels = 1
    fmt = struct.pack('<HHIIHHHHI', 0xfffe, channels, 48000, 0, len(encoded), 0, 34, 1024, 4)
    fmt += bytes.fromhex('d242e147ba368d4d88fc61654f8c836c')
    fmt += struct.pack('<III', 1, config, 0)
    chunks = [(b'fmt ', fmt), (b'fact', struct.pack('<III', 200, 16, 16))]
    if looping:
        chunks += [(b'smpl', struct.pack('<15I', 0, 0, 0, 60, 0, 0, 0, 1, 0, 0, 0, 48, 191, 0, 0))]
    chunks += [(b'data', encoded)]
    body = b'WAVE' + b''.join(name + struct.pack('<I', len(data)) + data + bytes(len(data) % 2) for name, data in chunks)
    return b'RIFF' + struct.pack('<I', len(body)) + body


class Atrac9Tests(unittest.TestCase):
    def check_file(self, data):
        with tempfile.TemporaryDirectory(prefix='Ketsui 音频 ') as directory:
            path = Path(directory) / '自制测试.at9'
            path.write_bytes(data)
            return subprocess.run([str(EXE), '--waveform-check', str(path)], capture_output=True, text=True, timeout=15)

    def test_authored_mono_delay_and_loop_are_decoded(self):
        for looping in (False, True):
            with self.subTest(looping=looping):
                result = self.check_file(authored_wave(looping=looping))
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                events = [json.loads(line) for line in result.stdout.splitlines()]
                parsed, decoded = events
                self.assertEqual(parsed['codec'], 'atrac9')
                self.assertEqual(parsed['delay'], 16)
                self.assertEqual(parsed['blocks'], 2 if looping else 1)
                self.assertEqual(decoded['decoded_frames'], 256)
                self.assertEqual(decoded['audible_frames'], 200)
                self.assertEqual(decoded['nonzero_samples'], 0)

    def test_bad_frames_and_truncated_riff_fail_without_success(self):
        valid = authored_wave()
        malformed = bytearray(valid)
        malformed[-64] = 0x40  # Illegal first-frame reuse flag.
        for data in (valid[:-1], bytes(malformed)):
            with self.subTest(size=len(data)):
                result = self.check_file(data)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertNotIn('native_waveform_check_complete', result.stdout)


if __name__ == '__main__':
    unittest.main()
