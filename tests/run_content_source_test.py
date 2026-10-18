"""Run the content-source test on owned temporary fixtures, never game data."""
from pathlib import Path
import subprocess
import sys
import tempfile

if len(sys.argv)!=2:
    raise SystemExit('Expected the native test executable')
with tempfile.TemporaryDirectory(prefix='cave-content-test-') as temp:
    raise SystemExit(subprocess.run([sys.argv[1],str(Path(temp)/'fixture')],timeout=20).returncode)
