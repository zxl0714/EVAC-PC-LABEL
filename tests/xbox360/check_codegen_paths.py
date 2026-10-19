"""ROM-free native check: UTF-8 manifest paths must reach the static XEX loader."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from analysis_paths import private_output
from project import ROOT, write_json_new


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generator', type=Path, required=True)
    args = parser.parse_args()
    output = private_output(ROOT / '.local/codegen-path-checks' / uuid.uuid4().hex)
    source = output / '输入中文'
    source.mkdir(parents=True)
    fixture = b'authored non-XEX fixture; must be rejected without guest execution'
    (source / 'default.xex').write_bytes(fixture)
    include = output / '声明中文😀.toml'
    include.write_text('# Authored empty include.\n', encoding='utf-8')
    generated = output / '输出中文😀'
    quoted = lambda value: json.dumps(str(value).replace('\\', '/'), ensure_ascii=False)
    manifest = output / 'manifest.toml'
    manifest.write_text('\n'.join(['[project]', 'name = "path_fixture"', 'sdk_version = "0.9.0.0"',
        'game_root = ' + quoted(source), '[entrypoint]', 'file_path = ' + quoted(source / 'default.xex'),
        'out_directory_path = ' + quoted(generated), 'includes = [' + quoted(include) + ']', '']), encoding='utf-8')
    result = subprocess.run([str(args.generator.resolve(strict=True)), 'codegen', str(manifest)],
        cwd=ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
    text = result.stdout.decode('utf-8', errors='replace')
    (output / 'stdout.log').write_bytes(result.stdout)
    if result.returncode == 0 or 'Failed to load entrypoint XEX' not in text:
        raise AssertionError('UTF-8 paths did not reach the expected static loader rejection: ' + str(output))
    if list(source.iterdir()) != [source / 'default.xex'] or (source / 'default.xex').read_bytes() != fixture:
        raise AssertionError('The native generator wrote into its input directory')
    write_json_new(output / 'result.json', dict(format=1, passed=True, guestExecution=False,
        scope='Authored invalid XEX reached the loader through UTF-8 input/include/output paths; not game code generation'))
    print('UTF-8 native path check passed; authored invalid XEX rejected; no guest execution.')


if __name__ == '__main__':
    main()
