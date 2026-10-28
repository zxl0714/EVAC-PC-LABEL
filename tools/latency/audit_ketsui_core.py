"""Pin the original Ketsui module's frame callback and input-delivery path."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from ps4.native_elf import Elf, plain_self_elf
from project import digest, write_json_new


def image_bytes(elf,address,size):
    matches=[s for s in elf.segments if s['address']<=address and
        address+size<=s['address']+s['file_size']]
    if len(matches)!=1:
        raise ValueError('Original instruction/data is not uniquely file-backed')
    segment=matches[0]
    start=segment['file_offset']+address-segment['address']
    return elf.raw[start:start+size]


def imported_target(elf,stub,provider):
    opcode=image_bytes(elf,stub,6)
    if opcode[:2]!=b'\xff\x25':
        raise ValueError('Not an original RIP-relative import thunk')
    got=stub+6+struct.unpack_from('<i',opcode,2)[0]
    relocation,=[r for r in elf.relocations if r['address']==got and r['type']==7]
    symbol=elf.symbols[relocation['symbol']]
    identity,=[s for s in elf.inventory()['imports'] if s['name']==symbol['name']]
    exported,=[s for s in provider.exports() if s['name'].split('#')[0]==symbol['name'].split('#')[0]
               and s['library']==identity['library'] and s['module']==identity['module']]
    return dict(stub=stub,got=got,nid=symbol['name'].split('#')[0],library=identity['library'],
                module=identity['module'],provider_offset=exported['value'])


def ordered(code,patterns):
    cursor=0;found=[]
    for pattern in patterns:
        match=next((i for i in range(cursor,len(code)) if re.search(pattern,code[i]['asm'])),None)
        if match is None:
            raise ValueError('Original instruction sequence differs: '+pattern)
        found.append(code[match]);cursor=match+1
    return found


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination',type=Path,required=True)
    args=parser.parse_args()
    destination=args.destination.resolve()
    if not destination.is_relative_to(ROOT/'.local/input-latency-v080-r1') or destination.exists():
        raise ValueError('Fresh private offline audit directory required')
    destination.mkdir()
    files={name:ROOT/'game-data/ketsui'/name for name in ('eboot.bin','ketsui_01000.prx','emu_integration_01400.prx')}
    images={name:Elf(plain_self_elf(path.read_bytes())) for name,path in files.items()}
    guest,provider=images['ketsui_01000.prx'],images['emu_integration_01400.prx']
    descriptor,=[s for s in guest.exports() if s['name'].split('#')[0]=='yOTrf47AywI']
    if (descriptor['type'],descriptor['value'],descriptor['size'])!=(1,0x50020,24):
        raise ValueError('Original module descriptor differs')
    initializer,=[r for r in guest.relocations if r['address']==0x50030 and r['type']==8]
    if initializer['addend']!=0x61b0:
        raise ValueError('Original descriptor initializer differs')
    frame,=[r for r in guest.relocations if r['address']==0x50088 and r['type']==8]
    if frame['addend']!=0x6610:
        raise ValueError('Original registered frame callback differs')
    imports={name:imported_target(guest,stub,provider) for name,stub in
        (('register_callbacks',0x258),('deliver_input',0x2f8),('bind_output_surface',0x188))}
    if imports['deliver_input']['provider_offset']!=0x5c0a0:
        raise ValueError('Original input delivery provider differs')
    objdump=ROOT/'tools/apps/llvm-23.1.3/bin/llvm-objdump.exe'
    def decode(name,start,stop):
        result=subprocess.run([str(objdump),'-d','--x86-asm-syntax=intel','--no-show-raw-insn',
            f'--start-address=0x{start:x}',f'--stop-address=0x{stop:x}',str(files[name])],
            text=True,encoding='utf-8',stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
        rows=[]
        for line in result.stdout.splitlines():
            m=re.match(r'^\s*([0-9a-f]+):\s+(.+)',line)
            if m:
                pc=int(m[1],16)
                if start<=pc<stop:
                    rows.append(dict(address=pc,asm=m[2]))
        if not rows or rows[0]['address']!=start:
            raise ValueError('Decoder did not return the pinned executable boundary')
        return rows
    callback=decode('ketsui_01000.prx',0x6610,0x6829)
    flow=ordered(callback,[r'mov\s+r13, rdi',r'mov\s+r15, rsi',r'mov\s+r12, rdx',
        r'mov\s+rdi, qword ptr \[r13 \+ 0x18\]',r'mov\s+rdx, qword ptr \[r15 \+ 0x18\]',
        r'call\s+0x2f8\b',r'mov\s+esi, 0x1',r'add\s+rdx, 0x28',r'call\s+0x2f8\b',
        r'mov\s+rsi, qword ptr \[r12 \+ 0x18\]',r'call\s+0x188\b',r'call\s+0x29610\b'])
    delivery=decode('emu_integration_01400.prx',0x5c0a0,0x5c17a)
    digital=ordered(delivery,[r'cmp\s+eax, 0x3',r'movzx\s+eax, byte ptr \[rdx \+ 0x8\]',
        r'movzx\s+edx, byte ptr \[rdx \+ 0x9\]',r'shl\s+edx, 0x8',r'mov\s+rdi, r14',
        r'mov\s+esi, ebx',r'jmp\s+0x5c260\b'])
    caller=decode('eboot.bin',0x29613,0x29640)
    caller_abi=ordered(caller,[r'mov\s+rdi, qword ptr \[r15 \+ 0x10\]',
        r'mov\s+rsi, qword ptr \[r15 \+ 0x18\]',r'mov\s+rdx, r15',
        r'call\s+qword ptr \[rax \+ 0x50\]',r'mov\s+rax, qword ptr \[r12\]'])
    report=dict(game='ketsui',guest_execution=False,original_data_modified=False,
        sources=[dict(path=str(p),sha256=digest(p),decoded_sha256=hashlib.sha256(images[n].raw).hexdigest())
                 for n,p in files.items()],objdump_sha256=digest(objdump),
        descriptor=descriptor,callback_table=0x50038,frame_slot=0x50088,frame_callback=0x6610,
        imports=imports,frame_input_then_core_order=flow,digital_type3_layout=digital,
        callback_instructions=callback,delivery_instructions=delivery,
        original_caller_abi=caller_abi,callback_return_address=0x29629,
        scope='Original callback ABI uses integer arguments context, inputs and output descriptor; input delivery precedes the first core-frame execution.',
        limitations=['Delivery is not yet the emulated CPU actual port read',
            'No input-to-first-response milliseconds or presentation ownership proved by this offline audit'])
    write_json_new(destination/'report.json',report)
    print(json.dumps(dict(output=str(destination/'report.json'),sha256=digest(destination/'report.json'),
        frame_slot=hex(report['frame_slot']),frame_callback=hex(report['frame_callback']),
        input_delivery=imports['deliver_input']),indent=2))


if __name__=='__main__':
    main()
