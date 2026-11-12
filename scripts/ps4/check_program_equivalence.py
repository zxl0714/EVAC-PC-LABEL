"""Check known PS4 exports through the native inspector, preparer and loader.

Inputs remain unchanged. Non-program assets are hard-linked into an isolated
fixture; program wrappers are rewritten there. No guest entry point is run.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from project import ROOT
from ps4.native_elf import plain_self_elf


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def stable_image(path):
    data = bytearray(path.read_bytes())
    flags = struct.unpack_from('<Q', data, 56)[0]
    actual = bytes(data[64:96]).hex()
    contract = bytes(data[-32:]).hex() if flags & 4 else actual
    if flags & 4:
        del data[-32:]
    struct.pack_into('<Q', data, 56, flags & ~4)
    data[64:96] = bytes(32)
    return hashlib.sha256(data).hexdigest(), actual, contract


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', action='append', required=True, metavar='GAME=PATH')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    work = args.output.resolve()
    if not work.is_relative_to(ROOT / '.local'):
        parser.error('Output must be an isolated directory below .local')
    work.mkdir(parents=True, exist_ok=False)
    native = ROOT / 'out/ps4-native/KetsuiNativeProbe.exe'
    inspect = ROOT / 'out/common/bin/cave_inspect.exe'
    results = []

    def run(command, log):
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
        log.with_suffix('.jsonl').write_text(result.stdout, encoding='utf-8')
        log.with_suffix('.stderr.txt').write_text(result.stderr, encoding='utf-8')
        if result.returncode:
            raise RuntimeError(f'{log}: exit {result.returncode}: {result.stderr[-1000:]}')
        return result.stdout

    for number, value in enumerate(args.data):
        game, path = value.split('=', 1)
        original = Path(path).resolve(strict=True)
        run_dir = work / f'{number}-{game}'
        run_dir.mkdir()
        def check(data, stem):
            result = json.loads(run([inspect, '--root', run_dir, '--game', game, '--data-dir', data, '--full'], run_dir / stem))['games'][0]
            assert result['dataState'] == 'verified', result
            return result
        known = check(original, 'original-inspect')
        manifest = json.loads((ROOT / f'games/{game}/data.json').read_text(encoding='utf-8'))
        version = next(v for v in [dict(manifest, id=manifest['variantId']), *manifest.get('alternatives', [])] if v['id'] == known['dataVariant'])
        rewritten = run_dir / 'data'
        count = 0
        original_hashes = {}
        for item in version['files']:
            source, target = original / item['path'], rewritten / item['path']
            target.parent.mkdir(parents=True, exist_ok=True)
            if item['program']:
                original_hashes[item['path']] = digest(source)
                data = bytearray(plain_self_elf(source.read_bytes()))
                # Change unused section metadata without changing program headers,
                # executable/data segments, imports, relocations or resource seeds.
                struct.pack_into('<Q', data, 40, len(data))
                struct.pack_into('<HHH', data, 58, 64, 1, 0)
                data.extend(bytes(64))
                target.write_bytes(data)
                assert digest(target) != original_hashes[item['path']]
                count += 1
            else:
                os.link(source, target)
        equivalent = check(rewritten, 'equivalent-inspect')
        assert equivalent['dataVariant'] == known['dataVariant']
        prepared = []
        for label, data in (('original', original), ('equivalent', rewritten)):
            collection = run_dir / label
            collection.mkdir()
            result = json.loads(run([native, '--prepare-game', '--root', collection, '--game', game, '--data-dir', data], run_dir / f'{label}-prepare').splitlines()[-1])
            prepared.append(result)
        a, b = prepared
        assert digest(Path(a['readFixes'])) == digest(Path(b['readFixes'])), 'Resource overlays changed'
        modules = sorted(Path(a['modules']).glob('*.ktmodule'))
        for first, second in [(Path(a['executable']), Path(b['executable'])), *[(p, Path(b['modules']) / p.name) for p in modules]]:
            x, raw_a, _ = stable_image(first)
            y, raw_b, contract = stable_image(second)
            assert x == y and raw_a != raw_b, f'Prepared payload differs: {first.name}'
            if first == Path(a['executable']):
                assert contract == raw_a, 'Patch identity lost the verified original program'
                assert raw_b == digest(rewritten / 'eboot.bin'), 'Actual source identity was overwritten'
        census = run([native, b['executable'], '--binding-census', '--module-dir', b['modules']], run_dir / 'binding-census')
        assert '"event":"native_binding_census_complete"' in census
        root_event = next(json.loads(line) for line in census.splitlines() if line.startswith('{') and '"event":"image_prepared"' in line)
        assert root_event['source_sha256'] == digest(rewritten / 'eboot.bin')
        assert root_event['contract_sha256'] == digest(original / 'eboot.bin')
        for path, sha in original_hashes.items():
            assert digest(original / path) == sha, 'Original program changed'
        result = dict(game=game, variant=known['dataVariant'], programs=count, modules=len(modules),
                      inspection=True, preparedPayloadsIdentical=True, resourcePatchesIdentical=True,
                      bindingCensus=True, sourceAndPatchIdentityVerified=True, originalsUnchanged=True)
        results.append(result)
        (work / 'result.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
