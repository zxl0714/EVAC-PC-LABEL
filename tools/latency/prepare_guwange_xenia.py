"""Stage original Guwange register observations for input/core/queue analysis."""
import argparse
import json
from pathlib import Path
import re
import shutil
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from ppc_functions import functions
from project import digest, write_json_new

# Discovery includes original polymorphic targets and interpreted-core entry.
# These are not yet declared to be exact gameplay input-consumption points.
SITES = {
    'default': [
        (1, 0x821E9644, (3, 31, 24, 32)),
        (2, 0x821E9648, (3, 31, 24, 32)),
        (3, 0x821E9664, (11, 3, 4, 31)),
        (4, 0x821E9668, (3, 31, 24, 32)),
        (5, 0x821E966C, (3, 31, 24, 32)),
        (6, 0x821E9670, (3, 31, 24, 32)),
        (7, 0x821E9698, (10, 3, 4, 31)),
        (8, 0x821E969C, (3, 31, 24, 32)),
        (9, 0x821E96AC, (11, 3, 4, 31)),
        (10, 0x821E96B0, (3, 31, 24, 32)),
        (11, 0x821F6E48, (30, 11, 10, 31)),
        (12, 0x821F6E54, (10, 30, 31, 1)),
        (13, 0x821F6D78, (4, 3, 31, 30)),
        (14, 0x821F6D7C, (3, 31, 27, 30)),
        (15, 0x821F6DB8, (21, 27, 30, 1)),
        # Before the flag store, NOT the Sleep(0) loop's back-edge.
        (16, 0x821F74A8, (27, 31, 29, 30)),
        (17, 0x821F74C0, (11, 31, 29, 30)),
        (18, 0x821F74D8, (3, 4, 31, 30)),
        (19, 0x8226568C, (3, 4, 31, 32)),
        (20, 0x82265690, (3, 4, 31, 32)),
        (21, 0x821F76E4, (8, 31, 11, 9)),
        (22, 0x821F77D8, (8, 31, 11, 9)),
        (23, 0x8219E10C, (11, 3, 4, 31)),
        (24, 0x8219E110, (3, 31, 30, 32)),
        (25, 0x8219E22C, (11, 3, 4, 31)),
        (26, 0x8219E230, (3, 31, 30, 32)),
        (28, 0x821CCE3C, (4, 5, 11, 32)),
    ],
    'Guwange01_x360': [(30, 0x88020000, (3, 4, 31, 32)),
                        (31, 0x88020224, (3, 4, 30, 32))],
}

# Exact original loads of arcade ports at context + D00010..D00013.
# MAME's cave.cpp supplies the hardware-map clue; original sections/opcodes
# and these effective-address constructions are checked independently below.
PORT_READS = [
    (40, 0x88093390, 0xD00010, 2, 11),
    (42, 0x882CE7A0, 0xD00010, 1, 11),
    (44, 0x882CE5E0, 0xD00011, 1, 11),
    (46, 0x882D0318, 0xD00012, 2, 11),
    (48, 0x8815CED8, 0xD00012, 2, 11),
    (50, 0x882D2088, 0xD00010, 1, 11),
    (52, 0x882D21A8, 0xD00011, 1, 11),
    (54, 0x882D2400, 0xD00011, 1, 3),
    (56, 0x882D257C, 0xD00011, 1, 4),
    (58, 0x882CEA30, 0xD00013, 1, 11),
    (60, 0x882D0250, 0xD00013, 1, 11),
    (62, 0x885CE740, 0xD00013, 1, 11),
    (64, 0x885CEA10, 0xD00013, 1, 11),
]
for identifier, pc, _, _, result in PORT_READS:
    SITES['Guwange01_x360'].extend([(identifier, pc, (11, 1, 5, 32)),
        (identifier + 1, pc + 4, (result, 1, 5, 32))])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--core-only', action='store_true',
                        help='Only input refresh and original arcade CPU entry/return')
    parser.add_argument('--profile-seed', type=Path,
                        help='Stopped private Xenia Guwange fixture, retaining its own saves')
    args = parser.parse_args()
    base = ROOT / '.local/input-latency-v080-r1'
    destination = args.destination.resolve()
    if not destination.is_relative_to(base) or destination.exists():
        raise ValueError('Fresh private fixture required')
    census_root = ROOT / '.local/guwange-analysis-r1/census-r3'
    receipt_path = census_root / 'analysis.json'
    receipt = json.loads(receipt_path.read_text())
    if receipt['guestExecution'] or not receipt['gameDataUnchanged']:
        raise ValueError('Original static census required')
    pinned, identities = [], {}
    selected_sites = {module: [s for s in specs if not args.core_only or s[0] in (1, 2) or s[0] >= 30]
                      for module, specs in SITES.items()}
    for module, specs in selected_sites.items():
        item, = [m for m in receipt['modules'] if Path(m['module']).stem == module]
        program = ROOT / 'game-data/guwange' / ('default.xex' if module == 'default' else f'module/{module}.dll')
        if digest(program) != item['inputSha256']:
            raise ValueError('Original XBLA program changed')
        census = json.loads((census_root / module / 'census.json').read_text())
        if census['guestExecution'] or census['gpuInitialized'] or census['audioInitialized']:
            raise ValueError('Static original sections required')
        expected = {d['path']: d['sha256'] for d in item['dumps']}
        sections = []
        for section in census['sections']:
            if not section['inImage']:
                continue
            relative = module + '/' + section['dump']
            path = census_root / relative
            if digest(path) != expected[relative]:
                raise ValueError('Original section identity changed')
            identities[str(path)] = expected[relative]
            sections.append((section['address'], path.read_bytes()))
        decoded = {pc: asm for _, insns, _ in functions('guwange', module) for pc, asm in insns}
        if module == 'Guwange01_x360':
            for _, pc, port, width, result in PORT_READS:
                load = 'lhz' if width == 2 else 'lbz'
                if decoded.get(pc) != f'{load} r{result},0(r11)':
                    raise ValueError('Original arcade-port load changed')
                preceding = [decoded.get(p, '') for p in range(pc - 32, pc, 4)]
                if (not any(re.fullmatch(r'addis r11,r11,208', s) for s in preceding) or
                    f'addi r11,r11,{port & 0xffff}' not in preceding):
                    raise ValueError('Original effective input-port address differs')
        for identifier, pc, registers in specs:
            if pc not in decoded:
                raise ValueError(f'Not an original instruction boundary: {pc:08X}')
            opcode, = [struct.unpack_from('>I', data, pc - start)[0] for start, data in sections
                       if start <= pc and pc + 4 <= start + len(data)]
            if identifier == 19:
                displacement = opcode & 0x03fffffc
                if displacement & 0x02000000:
                    displacement -= 0x04000000
                target = (displacement if opcode & 2 else pc + displacement) & 0xffffffff
                imported, = [i for i in census['imports'] if i['name'] == 'VdSwap']
                if opcode >> 26 != 18 or not opcode & 1 or target != imported['thunkAddress']:
                    raise ValueError('Not the original VdSwap import')
            pinned.append(dict(module=module, id=identifier, address=pc, opcode=opcode,
                registers=registers, asm=decoded[pc], program_sha256=item['inputSha256']))
    executable = base / 'xenia-futari-pixels-build-r1/xenia_canary.exe'
    build = json.loads((executable.parent / 'build-result.json').read_text())
    if digest(executable) != build['exe_sha256']:
        raise ValueError('Pinned generic observer changed')
    template = base / 'xenia-futari'
    profile_seed = args.profile_seed.resolve(strict=True) if args.profile_seed else template
    if not profile_seed.is_relative_to(base):
        raise ValueError('Private test profile seed required')
    if args.profile_seed:
        seeded = json.loads((profile_seed / 'probe-stage.json').read_text())
        if not any(s['program_sha256'] == 'b426e877eec7e34afe0c1314390a8f11c1728053668b6aea2efed2387d1bf401'
                   for s in seeded['sites']):
            raise ValueError('Seed must be a Guwange original-input fixture')
    config = (template / 'xenia-canary.config.toml').read_text(encoding='utf-8')
    for pattern, replacement in (
        (r'^storage_root\s*=\s*"[^"]*"', 'storage_root = ' + json.dumps(destination.as_posix())),
        (r'^license_mask\s*=\s*\d+', 'license_mask = 1')):
        config, count = re.subn(pattern, replacement, config, flags=re.M)
        if count != 1:
            raise ValueError('Pinned configuration anchor changed')
    destination.mkdir()
    (destination / 'xenia-canary.config.toml').write_text(config, encoding='utf-8')
    for relative in ('xconfig.settings', 'content' if args.profile_seed else 'content/E03000002770A370/FFFE07D1'):
        source, output = profile_seed / relative, destination / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        if source.is_dir():
            shutil.copytree(source, output)
        else:
            shutil.copy2(source, output)
    sites = destination / 'guest-sites.txt'
    sites.write_text(''.join(f"{s['module']} {s['address']:08X} {s['opcode']:08X} {s['id']} " +
        ' '.join(map(str, s['registers'])) + '\n' for s in pinned), encoding='ascii')
    write_json_new(destination / 'probe-stage.json', dict(executable=str(executable),
        exe_sha256=digest(executable), sites=pinned, sections=identities,
        census=dict(path=str(receipt_path), sha256=digest(receipt_path)),
        guest_sites_sha256=digest(sites), config_sha256=digest(destination / 'xenia-canary.config.toml'),
        source_sha256=digest(Path(__file__)), guest_memory_modified=False,
        game_policy_modified=False, original_profile_modified=False,
        core_only=args.core_only, profile_seed=str(profile_seed),
        arcade_port_reads=PORT_READS,
        profile_files={p.relative_to(profile_seed).as_posix(): digest(p)
            for p in (profile_seed / 'content').rglob('*') if p.is_file()},
        scene='Arcade / Shishin / Stage1; original timing; initial core-target discovery'))
    print(destination)


if __name__ == '__main__':
    main()
