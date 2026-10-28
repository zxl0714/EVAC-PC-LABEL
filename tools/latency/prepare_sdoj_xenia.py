"""Stage a private original-disc SDOJ Xenia observer using verified section dumps."""
import argparse
import json
from pathlib import Path
import re
import shutil
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from project import digest, write_json_new
from ppc_functions import functions

SITES = {
    'default': [
        (1, 0x82178B78, (3, 4, 32, 1)),
        (2, 0x82178BFC, (26, 31, 32, 1)),
        (10, 0x8219DCFC, (31, 11, 9, 8)),
        (11, 0x8219DDE8, (31, 11, 9, 8)),
        (12, 0x8219DEA4, (11, 10, 4, 3)),
        (13, 0x8219DEC8, (11, 9, 10, 4)),
        (14, 0x8219D148, (3, 4, 32, 1)),
        (15, 0x8219D9B0, (31, 29, 11, 32)),
        (16, 0x8219D9C8, (31, 29, 11, 32)),
        (17, 0x821DFECC, (3, 31, 32, 1)),
        (18, 0x821DFED0, (3, 31, 30, 32)),
    ],
    'CA022100': [
        (3, 0x880344B4, (28, 29, 8, 32)),
        (23, 0x880344CC, (28, 29, 10, 32)),
        (4, 0x88051D88, (10, 9, 11, 27)),
        (5, 0x880340D8, (11, 28, 29, 32)),
        (6, 0x8803417C, (11, 27, 28, 32)),
        (7, 0x88033178, (4, 7, 11, 32)),
        (8, 0x88031288, (4, 3, 11, 30)),
        (9, 0x880313C0, (3, 30, 31, 32)),
        (19, 0x88051350, (3, 4, 32, 1)),
        (20, 0x880513D0, (3, 4, 32, 1)),
        (21, 0x88051E14, (10, 8, 7, 30)),
    ],
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--census', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    base = ROOT / '.local/input-latency-v080-r1'
    destination = args.destination.resolve()
    if not destination.is_relative_to(base) or destination.exists():
        raise ValueError('Fresh private destination required')
    census_root = args.census.resolve(strict=True)
    receipt_path = census_root / 'analysis.json'
    receipt = json.loads(receipt_path.read_text())
    if receipt['guestExecution'] or not receipt['selectedProgramsUnchanged']:
        raise ValueError('Static original-input census required')
    inputs = {Path(p['path']).stem: p for p in receipt['programs']}
    sections_identity, pinned = {}, []
    for module, specs in SITES.items():
        program = ROOT / 'game-data/sdoj' / inputs[module]['path']
        if digest(program) != inputs[module]['sha256']:
            raise ValueError('Original disc program identity changed')
        item, = [m for m in receipt['modules'] if Path(m['module']).stem == module]
        expected = {str((census_root / d['path']).resolve()): d['sha256'] for d in item['dumps']}
        census_path = census_root / module / 'census.json'
        census = json.loads(census_path.read_text())
        if any(census[k] for k in ('guestExecution', 'audioInitialized', 'gpuInitialized')):
            raise ValueError('Expected tool-mode sections, not a gameplay memory dump')
        sections = []
        for section in census['sections']:
            if not section['inImage']:
                continue
            path = (census_path.parent / section['dump']).resolve()
            if digest(path) != expected[str(path)]:
                raise ValueError('Original section dump changed')
            sections_identity[str(path)] = expected[str(path)]
            sections.append((section['address'], path.read_bytes()))
        decoded = {pc: asm for _, insns, _ in functions('sdoj', module) for pc, asm in insns}
        for identifier, pc, registers in specs:
            if pc not in decoded:
                raise ValueError(f'Not an original instruction boundary: {pc:08X}')
            op, = [struct.unpack_from('>I', data, pc - address)[0] for address, data in sections
                    if address <= pc and pc + 4 <= address + len(data)]
            if identifier == 17:
                displacement = op & 0x03fffffc
                if displacement & 0x02000000:
                    displacement -= 0x04000000
                target = (displacement if op & 2 else pc + displacement) & 0xffffffff
                imported, = [i for i in census['imports'] if i['name'] == 'VdSwap']
                if op >> 26 != 18 or not op & 1 or target != imported['thunkAddress']:
                    raise ValueError('Swap site is not the original VdSwap import')
            pinned.append(dict(module=module, id=identifier, address=pc, opcode=op,
                registers=registers, asm=decoded[pc], program_sha256=inputs[module]['sha256']))
    executable = base / 'xenia-futari-pixels-build-r1/xenia_canary.exe'
    build_receipt = json.loads((executable.parent / 'build-result.json').read_text())
    if digest(executable) != build_receipt['exe_sha256']:
        raise ValueError('Pinned generic observer executable changed')
    template = base / 'xenia-futari'
    config, count = re.subn(r'^storage_root\s*=\s*"[^"]*"',
        'storage_root = ' + json.dumps(destination.as_posix()),
        (template / 'xenia-canary.config.toml').read_text(encoding='utf-8'), flags=re.M)
    if count != 1:
        raise ValueError('Configuration storage-root anchor changed')
    destination.mkdir()
    (destination / 'xenia-canary.config.toml').write_text(config, encoding='utf-8')
    for relative in ('xconfig.settings', 'content/E03000002770A370/FFFE07D1'):
        source, output = template / relative, destination / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        if source.is_dir():
            shutil.copytree(source, output)
        else:
            shutil.copy2(source, output)
    sites_path = destination / 'guest-sites.txt'
    sites_path.write_text(''.join(f"{s['module']} {s['address']:08X} {s['opcode']:08X} {s['id']} " +
        ' '.join(map(str, s['registers'])) + '\n' for s in pinned), encoding='ascii')
    write_json_new(destination / 'probe-stage.json', dict(executable=str(executable),
        exe_sha256=digest(executable), sites=pinned, sections=sections_identity,
        census=dict(path=str(receipt_path), sha256=digest(receipt_path)),
        guest_sites_sha256=digest(sites_path), config_sha256=digest(destination / 'xenia-canary.config.toml'),
        source_sha256=digest(Path(__file__)), guest_memory_modified=False,
        game_policy_modified=False, original_profile_modified=False,
        scene='Arcade HD / Training / Stage1 / Type-A / Shot; original 3V'))
    print(destination)


if __name__ == '__main__':
    main()
