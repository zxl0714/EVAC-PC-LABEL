"""Pin original Daioujou callback/input order, including its tail dispatch ABI."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from ps4.native_elf import Elf,plain_self_elf
from project import digest,write_json_new
from audit_ketsui_core import imported_target,ordered


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination',type=Path,required=True)
    parser.add_argument('--module',choices=('daioujou_01000.prx','daioujou_01030.prx'),
                        default='daioujou_01000.prx')
    args=parser.parse_args();destination=args.destination.resolve()
    if not destination.is_relative_to(ROOT/'.local/input-latency-v080-r1') or destination.exists():
        raise ValueError('Fresh private audit output required')
    files={n:ROOT/'game-data/daioujou'/n for n in ('eboot.bin',args.module,'emu_integration_01700.prx')}
    images={n:Elf(plain_self_elf(p.read_bytes())) for n,p in files.items()}
    module,provider=images[args.module],images['emu_integration_01700.prx']
    descriptor,=module.exports()
    descriptor_name,core_step={
        'daioujou_01000.prx':('pA6W+oE6IaQ#T#A',0x259d0),
        'daioujou_01030.prx':('fvHOEIbLSvc#T#A',0x2fa50),
    }[args.module]
    if (descriptor['name'],descriptor['type'],descriptor['value'],descriptor['size'])!=(descriptor_name,1,0x400020,24):
        raise ValueError('Original Daioujou descriptor differs')
    rel={r['address']:r for r in module.relocations if r['type']==8}
    if rel[0x400030]['addend']!=0x62a0 or rel[0x400088]['addend']!=0x6700:
        raise ValueError('Original initializer/frame slot differs')
    imports={name:imported_target(module,stub,provider) for name,stub in
        (('register_callbacks',0x178),('deliver_input',0x2c8),('bind_output_surface',0x2f8))}
    if imports['deliver_input']['provider_offset']!=0x61250:
        raise ValueError('Original newer input integration differs')
    objdump=ROOT/'tools/apps/llvm-23.1.3/bin/llvm-objdump.exe'
    def decode(name,start,stop):
        result=subprocess.run([str(objdump),'-d','--x86-asm-syntax=intel','--no-show-raw-insn',
            f'--start-address=0x{start:x}',f'--stop-address=0x{stop:x}',str(files[name])],
            text=True,encoding='utf-8',capture_output=True,check=True)
        rows=[]
        for line in result.stdout.splitlines():
            match=re.match(r'^\s*([0-9a-f]+):\s+(.+)',line)
            if match and start<=int(match[1],16)<stop:
                rows.append(dict(address=int(match[1],16),asm=match[2]))
        if not rows or rows[0]['address']!=start: raise ValueError('Original decode boundary differs')
        return rows
    initializer=decode(args.module,0x62a0,0x62c5)
    ordered(initializer,[r'lea\s+rsi,.*# 0x400038',r'call\s+0x178\b'])
    frame=decode(args.module,0x6700,0x681b)
    flow=ordered(frame,[r'mov\s+r14, rdx',r'mov\s+r15, rdi',
        r'mov\s+rdi, qword ptr \[rdi \+ 0x18\]',r'mov\s+rdx, qword ptr \[rsi \+ 0x18\]',
        r'call\s+0x2c8\b',r'mov\s+esi, 0x1',r'add\s+rdx, 0x28',r'call\s+0x2c8\b',
        r'mov\s+rsi, r14',r'call\s+0x68c0\b'])
    helper=decode(args.module,0x68c0,0x6952)
    helper_flow=ordered(helper,[r'mov\s+rsi, qword ptr \[rsi \+ 0x18\]',r'call\s+0x2f8\b',
        r'call\s+0x308\b',rf'call\s+0x{core_step:x}\b'])
    delivery=decode('emu_integration_01700.prx',0x61250,0x61316)
    digital=ordered(delivery,[r'cmp\s+eax, 0x3',r'movzx\s+esi, byte ptr \[rdx \+ 0x9\]',
        r'movzx\s+eax, byte ptr \[rdx \+ 0x8\]',r'shl\s+esi, 0x8',r'or\s+esi, eax',
        r'vmovd\s+edx, xmm0',r'or\s+edx, esi',r'mov\s+esi, ebx',r'jmp\s+0x613f0\b'])
    caller=decode('eboot.bin',0x27bf5,0x27c35)
    direct=ordered(caller,[r'mov\s+rdi, qword ptr \[rbx \+ 0x10\]',
        r'mov\s+rsi, qword ptr \[rbx \+ 0x18\]',r'call\s+qword ptr \[rax \+ 0x50\]'])
    tail=ordered(caller,[r'mov\s+rax, qword ptr \[rax \+ 0x50\]',r'add\s+rbx, 0x30',
        r'mov\s+rdx, rbx',r'jmp\s+rax\b'])
    parent=decode('eboot.bin',0x1b541,0x1b55e)
    parent_flow=ordered(parent,[r'mov\s+rdi, r14',r'call\s+0x54c90\b',
        r'movzx\s+ecx, r15b',r'mov\s+rdi, rbx',r'mov\s+esi, r12d',
        r'mov\s+rdx, rax',r'call\s+0x27b70\b'])
    report=dict(game='daioujou',module_name=args.module,core_step=core_step,
        guest_execution=False,original_data_modified=False,
        sources=[dict(path=str(p),sha256=digest(p),decoded_sha256=hashlib.sha256(images[n].raw).hexdigest()) for n,p in files.items()],
        objdump_sha256=digest(objdump),descriptor=descriptor,initializer=initializer,
        callback_table=0x400038,frame_slot=0x400088,frame_callback=0x6700,imports=imports,
        frame_input_then_core_order=flow,frame_helper_order=helper_flow,digital_type3_layout=digital,
        callback_instructions=frame,helper_instructions=helper,delivery_instructions=delivery,
        original_caller_abi=direct,normal_tail_dispatch=tail,direct_callback_return=0x27c04,
        original_parent_caller=parent_flow,tail_callback_return=0x1b55e,
        fixed_return_address_not_valid_for_tail_dispatch=True,
        scope='Original context/inputs/output integer ABI; input delivered before core execution, with explicit normal tail dispatch',
        limitations=['New integration transforms high digital bits; raw masks must be verified, not copied from Ketsui',
            'No emulated CPU port consumption, first response or Present time proved offline'])
    destination.mkdir();write_json_new(destination/'report.json',report)
    print(json.dumps(dict(output=str(destination/'report.json'),sha256=digest(destination/'report.json'),
        frame_slot=hex(report['frame_slot']),frame_callback=hex(report['frame_callback']),
        input_delivery=imports['deliver_input']),indent=2))


if __name__=='__main__': main()
