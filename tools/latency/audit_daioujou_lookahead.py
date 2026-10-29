"""Audit original Daioujou look-ahead selection across all four bundled cores.

Decodes original files without executing or modifying them. Reports the boolean
setter, frame wrapper, save/restore calls and outer machine-profile call sites.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'scripts'), str(Path(__file__).parent)]
from ps4.native_elf import Elf, plain_self_elf
from audit_ketsui_core import image_bytes, imported_target, ordered
from project import digest, write_json_new


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    destination = args.destination.resolve()
    if not destination.is_relative_to(ROOT/'.local/input-latency-v080-r1') or destination.exists():
        raise ValueError('Fresh private offline audit directory required')
    destination.mkdir()
    objdump = ROOT/'tools/apps/llvm-23.1.3/bin/llvm-objdump.exe'
    files = {name: ROOT/'game-data/daioujou'/name for name in
             ('eboot.bin', 'emu_integration_01700.prx') +
             tuple(f'daioujou_010{n}0.prx' for n in range(4))}
    images = {name: Elf(plain_self_elf(path.read_bytes())) for name, path in files.items()}

    def decode(name, start=None, stop=None):
        path = destination/(name+'.elf')
        if not path.exists():
            path.write_bytes(images[name].raw)
        command = [str(objdump), '-d', '--x86-asm-syntax=intel', '--no-show-raw-insn']
        if start is not None:
            command += [f'--start-address=0x{start:x}', f'--stop-address=0x{stop:x}']
        result = subprocess.run(command+[str(path)], check=True, capture_output=True,
                                text=True, encoding='utf-8')
        rows = []
        for line in result.stdout.splitlines():
            match = re.match(r'^\s*([0-9a-f]+):\s+(.+)', line)
            if match:
                pc = int(match[1], 16)
                if start is None or start <= pc < stop:
                    rows.append(dict(address=pc, asm=match[2]))
        if not rows or (start is not None and rows[0]['address'] != start):
            raise ValueError('Original instruction boundary differs')
        return rows

    provider = images['emu_integration_01700.prx']
    modules = []
    for name, image in images.items():
        if not name.startswith('daioujou_'):
            continue
        imports = image.inventory()['imports']
        if any(s['name'].split('#')[0] == 'WslcK1FQcGI' for s in imports):
            raise ValueError('Core unexpectedly imports machine-profile query')
        callback, = [r for r in image.relocations if r['type'] == 8 and r['address'] == 0x200058]
        if callback['addend'] != 0x69c0:
            raise ValueError('Original low-latency setter pointer differs')
        setter = decode(name, 0x69c0, 0x69d1)
        selection = ordered(setter, [r'lea\s+rcx,', r'xor\s+eax, eax', r'test\s+esi, esi',
                                     r'setne\s+al', r'mov\s+dword ptr \[rcx\], eax', r'ret'])
        if len(setter) != len(selection):
            raise ValueError('Unexpected additional low-latency selection logic')
        setting = int(re.search(r'# 0x([0-9a-f]+)', setter[0]['asm'])[1], 16)
        helper = decode(name, 0x68c0, 0x6952)
        wrapper_targets = [int(re.search(r'(?:call|jmp)\s+0x([0-9a-f]+)', r['asm'])[1], 16)
                           for r in helper if re.search(r'(?:call|jmp)\s+0x[0-9a-f]+', r['asm'])
                           and int(re.search(r'0x([0-9a-f]+)', r['asm'])[1], 16) > 0x10000]
        if len(wrapper_targets) != 2 or len(set(wrapper_targets)) != 1:
            raise ValueError('Original core frame wrapper differs')
        wrapper = wrapper_targets[0]
        body = decode(name, wrapper, wrapper+0x1c3)
        flow = ordered(body, [rf'mov\s+r15d,.*# 0x{setting:x}\b', r'mov\s+ebx, 0x40',
                             r'call\s+0x858\b', r'test\s+r15d, r15d',
                             rf'call\s+0x{wrapper+0x1d0:x}\b', r'dec\s+r15d',
                             r'call\s+0x858\b', r'call\s+0x858\b',
                             rf'call\s+0x{wrapper+0x430:x}\b'])
        imports_used = {key: imported_target(image, stub, provider) for key, stub in
                        (('core_step', 0x858), ('prediction_side_effect_mode', 0x868),
                         ('outer_extra_frames', 0x2d8))}
        if imports_used['core_step']['nid'] != 'DZX8XVO+jNY':
            raise ValueError('Original integration step differs')
        constants = []
        for row in body:
            match = re.search(r'vmovsd\s+xmm[01], qword ptr.*# 0x([0-9a-f]+)', row['asm'])
            if match:
                address = int(match[1], 16)
                constants.append(dict(address=address, value=struct.unpack('<d', image_bytes(image,address,8))[0]))
        frame = decode(name, 0x6700, 0x681b)
        ordered(frame, [r'call\s+0x2c8\b', r'call\s+0x2c8\b', r'call\s+0x2d8\b',
                        r'call\s+0x68c0\b'])
        modules.append(dict(module=name, setter_slot=0x200058, setter=0x69c0,
            setting_address=setting, values_written_by_setter=[0,1], machine_profile_imports=[],
            selector_instructions=setter, helper_instructions=helper, frame_wrapper=wrapper,
            frame_callback_instructions=frame,
            original_save=wrapper+0x1d0, original_restore=wrapper+0x430,
            frame_flow=flow, frame_wrapper_instructions=body, constants=constants,
            imports=imports_used, normal_update_slices=64))

    # The outer executable DOES query Neo. Keep that separate from the cores'
    # boolean selection, and retain the actual callers for inspection.
    image = images['eboot.bin']
    got = {r['address']: image.symbols[r['symbol']]['name'] for r in image.relocations if r['type']==7}
    stubs = []
    for segment in image.segments:
        if not segment['flags'] & 1:
            continue
        block = image_bytes(image, segment['address'], segment['file_size'])
        for match in re.finditer(b'\xff\x25', block):
            position = match.start()
            if position+6 > len(block):
                continue
            address = segment['address']+position
            target = address+6+struct.unpack_from('<i',block,position+2)[0]
            if got.get(target,'').split('#')[0] == 'WslcK1FQcGI':
                stubs.append(address)
    outer = decode('eboot.bin')
    callers = []
    for index, row in enumerate(outer):
        if any(re.search(rf'\b(?:call|jmp)\s+0x{s:x}\b', row['asm']) for s in stubs):
            callers.append(dict(site=row['address'], context=outer[max(0,index-10):index+22]))
    if stubs != [0x231f70] or len(callers) != 6:
        raise ValueError('Outer machine-profile import references differ')
    extra = modules[0]['imports']['outer_extra_frames']['provider_offset']
    extra_code = decode('emu_integration_01700.prx', extra, extra+0x27)
    key_address = int(re.search(r'# 0x([0-9a-f]+)', extra_code[2]['asm'])[1], 16)
    key = image_bytes(provider, key_address, len('console.overdrive')+1)
    if key != b'console.overdrive\0':
        raise ValueError('Separate outer prediction option differs')
    prior_path = ROOT/'.local/input-latency-v080-r1/v081-daioujou-input-phase-r1.json'
    prior = json.loads(prior_path.read_text())
    for source in prior['sources']:
        if digest(Path(source['path'])) != source['sha256']:
            raise ValueError('Earlier original response evidence changed')
    report = dict(game='daioujou', guest_execution=False, original_data_modified=False,
        modules=modules, outer_neo_import_stubs=stubs, outer_neo_callers=callers,
        separate_outer_extra_frames=dict(setting='console.overdrive',
            instructions=extra_code, distinct_from_boolean_core_setting=True),
        earlier_response=dict(path=str(prior_path),sha256=digest(prior_path),
            pad_to_present_ms=prior['response_present_ms'],
            following_callback_ms=prior['following_callback_ms'],
            note='Previously inspected actual first response, not a new multi-sample test'),
        sources=[dict(path=str(path), sha256=digest(path),
                      decoded_sha256=hashlib.sha256(images[name].raw).hexdigest()) for name,path in files.items()],
        objdump_sha256=digest(objdump), generator_sha256=digest(Path(__file__)),
        limitations=['Static audit does not read the live low-latency option',
                     'No claim that forcing an unsupported second prediction is safe or faster',
                     'Normal 64 slices divide one update; they are not 64 game frames'])
    write_json_new(destination/'report.json', report)
    for module in modules:
        print(module['module'], 'boolean selector', hex(module['setting_address']),
              'wrapper', hex(module['frame_wrapper']), 'save/restore',
              hex(module['original_save']), hex(module['original_restore']))
    print('Outer Neo callers:', [hex(c['site']) for c in callers])


if __name__ == '__main__':
    main()
