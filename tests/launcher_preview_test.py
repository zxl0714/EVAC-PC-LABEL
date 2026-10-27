"""Render the real launcher without opening a window or requiring game data."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def main():
    executable = Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='evac-preview-') as directory:
        root = Path(directory)
        for language in ('zh', 'en', 'ja'):
            output = root / f'{language}.png'
            command = [str(executable), '--root', str(root), '--select', 'feveron',
                       '--language', language, '--render-preview', str(output)]
            result = subprocess.run(command, capture_output=True, timeout=15,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            assert result.returncode == 0, (language, result.stderr)
            png = output.read_bytes()
            assert png[:8] == b'\x89PNG\r\n\x1a\n'
            assert struct.unpack_from('>II', png, 16) == (1080, 900)
            # A failed preview must return without blocking on a modal dialog.
            repeated = subprocess.run(command, capture_output=True, timeout=5,
                                      creationflags=subprocess.CREATE_NO_WINDOW)
            assert repeated.returncode == 2
            assert output.read_bytes() == png
    print('Launcher preview: CN/EN/JP, Coming Soon card and failure path passed')


if __name__ == '__main__':
    main()
