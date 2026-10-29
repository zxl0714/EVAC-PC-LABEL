"""Audit the two original Dangun Feveron cores without changing game data."""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'scripts'), str(Path(__file__).parent)]
from ps4.native_elf import Elf, plain_self_elf
from audit_ketsui_core import image_bytes, imported_target, ordered
from project import digest, write_json_new

PINNED = {
    'dfeveron_01000.prx': '23a7b45b193aefe4fc808e4b41f3e65846d19307d56bcf45ba901a02778e8d81',
    'dfeveron_01010.prx': '65b1e7e91df15799b3c2c7450d3af336e23936d7cd0eecb4cbf34c026c86eaa6',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-data', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    destination = args.destination.resolve()
    if not destination.is_relative_to(ROOT / '.local/input-latency-v080-r1') or destination.exists():
        raise ValueError('Fresh private audit directory required')
    paths = {name: args.game_data / name for name in PINNED}
    for name, path in paths.items():
        if digest(path) != PINNED[name]:
            raise ValueError('Unknown original core: ' + name)
    provider_path = args.game_data / 'emu_integration_01200.prx'
    provider = Elf(plain_self_elf(provider_path.read_bytes()))
    objdump = ROOT / 'tools/apps/llvm-23.1.3/bin/llvm-objdump.exe'
    destination.mkdir()
    modules = []
    for name, path in paths.items():
        image = Elf(plain_self_elf(path.read_bytes()))
        decoded = destination / (name + '.elf')
        decoded.write_bytes(image.raw)

        def decode(start, stop):
            result = subprocess.run([str(objdump), '-d', '--x86-asm-syntax=intel',
                '--no-show-raw-insn', f'--start-address=0x{start:x}',
                f'--stop-address=0x{stop:x}', str(decoded)], check=True,
                capture_output=True, text=True, encoding='utf-8')
            rows = []
            for line in result.stdout.splitlines():
                match = re.match(r'^\s*([0-9a-f]+):\s+(.+)', line)
                if match and start <= int(match[1], 16) < stop:
                    rows.append(dict(address=int(match[1], 16), asm=match[2]))
            if not rows or rows[0]['address'] != start:
                raise ValueError('Original instruction boundary differs')
            return rows

        # Resolve the machine query from this core's own import thunk/GOT.
        opcode = image_bytes(image, 0x240, 6)
        if opcode[:2] != b'\xff\x25':
            raise ValueError('Original profile thunk differs')
        got = 0x246 + struct.unpack_from('<i', opcode, 2)[0]
        relocation, = [r for r in image.relocations if r['address'] == got and r['type'] == 7]
        if image.symbols[relocation['symbol']]['name'].split('#')[0] != 'WslcK1FQcGI':
            raise ValueError('Original profile import differs')
        callback, = [r for r in image.relocations if r['address'] == 0x1988070 and r['type'] == 8]
        if callback['addend'] != 0xd40:
            raise ValueError('Original frame callback differs')
        setter = decode(0xfc0, 0x1000)
        ordered(setter, [r'test\s+esi, esi', r'call\s+0x240\b', r'test\s+eax, eax',
                         r'setne\s+al', r'movzx\s+eax, al'])
        if name == 'dfeveron_01000.prx':
            ordered(setter, [r'mov\s+dword ptr \[rcx\], 0x2', r'mov\s+dword ptr \[rdx\], eax'])
            counts = dict(base=2, neo=2)
        else:
            ordered(setter, [r'lea\s+ecx, \[rax \+ 0x2\]', r'mov\s+dword ptr \[rdx\], ecx',
                             r'mov\s+dword ptr \[rdx\], eax'])
            counts = dict(base=2, neo=3)
        frame = decode(0xd40, 0xe5c)
        ordered(frame, [r'call\s+0x290\b', r'call\s+0x290\b', r'call\s+0x120\b', r'call\s+0xf00\b'])
        delta = -0xc0 if name == 'dfeveron_01000.prx' else 0
        save, restore, wrapper = 0x171c0 + delta, 0x173e0 + delta, 0x17060 + delta
        body = decode(wrapper, 0x171b9 + delta)
        flow = ordered(body, [r'call\s+0x730\b', r'call\s+0x660\b', rf'call\s+0x{save:x}\b',
            r'mov\s+ecx, 0xffffffff', r'cmovle\s+ecx, ebx', r'cmp\s+dword ptr \[rdx\], 0x0',
            r'xor\s+ecx, ecx', r'add\s+edx, ecx', r'call\s+0x730\b',
            r'call\s+0x730\b', rf'call\s+0x{restore:x}\b', r'jmp\s+0x660\b'])
        # At count>1 the base branch removes one speculative update; NEO
        # retains the full count. These are core updates, not latency in V.
        effective = dict(base=counts['base'] - 1, neo=counts['neo'])
        modules.append(dict(module=name, sha256=PINNED[name], callback_slot=0x1988070,
            frame_callback=0xd40, setter=0xfc0, count_address=0x1988098,
            profile_flag_address=0x1a88610, enabled_counts=counts,
            enabled_speculative_updates=effective, disabled_count=0,
            machine_import=dict(thunk=0x240, got=got, nid='WslcK1FQcGI'),
            imports={key: imported_target(image, stub, provider) for key, stub in
                [('core_step', 0x730), ('prediction_side_effect_mode', 0x660)]},
            frame_instructions=frame, setter_instructions=setter, wrapper_flow=flow,
            wrapper_instructions=body, wrapper=wrapper, original_save=save, original_restore=restore,
            prediction_duration=struct.unpack('<d', image_bytes(image, 0x1c1a8 + delta, 8))[0]))
    report = dict(game='feveron', original_data_modified=False, guest_execution=False,
        modules=modules, provider_sha256=digest(provider_path), objdump_sha256=digest(objdump),
        scope='Original save/predict/restore selection. Static update counts are not measured input latency; pair with inspected first-response images.')
    write_json_new(destination / 'report.json', report)
    print(json.dumps([{k: row[k] for k in ('module', 'enabled_counts', 'enabled_speculative_updates')}
                      for row in modules], indent=2))


if __name__ == '__main__':
    main()
