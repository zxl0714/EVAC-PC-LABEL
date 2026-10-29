"""Audit the original ESP Ra.De. input, frame callback and core-step imports."""
import argparse,hashlib,json,re,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'scripts'),str(Path(__file__).parent)]
from ps4.native_elf import Elf,plain_self_elf
from project import digest,write_json_new
from audit_ketsui_core import imported_target,ordered,image_bytes

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--destination',type=Path,required=True);a=p.parse_args();out=a.destination.resolve()
 if not out.is_relative_to(ROOT/'.local/input-latency-v080-r1') or out.exists():raise ValueError('Fresh private audit directory required')
 files={n:ROOT/'game-data/esprade'/n for n in ('eboot.bin','esprade_01030.prx','emu_integration_01500.prx')}
 images={n:Elf(plain_self_elf(path.read_bytes())) for n,path in files.items()};g=images['esprade_01030.prx'];provider=images['emu_integration_01500.prx'];desc,=g.exports()
 if (desc['name'],desc['value'],desc['size'])!=('aeN7R7LXCLw#R#A',0x3400020,24):raise ValueError('Original module descriptor differs')
 rel={r['address']:r['addend'] for r in g.relocations if r['type']==8}
 if (rel[0x3400030],rel[0x3400088])!=(0x910,0xe80):raise ValueError('Original frame table differs')
 imports={name:imported_target(g,stub,provider) for name,stub in [('register_callbacks',0x1c8),('deliver_input',0x2b8),('extra_frames',0x108),('frame_duration',0x118),('core_step',0x788),('bind_output',0xc8)]}
 if [imports[n]['provider_offset'] for n in ('deliver_input','extra_frames','core_step')]!=[0x5cc80,0x5ca70,0x66f60]:raise ValueError('Integration provider differs')
 out.mkdir();objdump=ROOT/'tools/apps/llvm-23.1.3/bin/llvm-objdump.exe'
 def decode(name,start,stop):
  elf=out/(name+'.elf')
  if not elf.exists():elf.write_bytes(images[name].raw)
  r=subprocess.run([str(objdump),'-d','--x86-asm-syntax=intel','--no-show-raw-insn',f'--start-address=0x{start:x}',f'--stop-address=0x{stop:x}',str(elf)],check=True,text=True,encoding='utf-8',capture_output=True)
  rows=[]
  for line in r.stdout.splitlines():
   m=re.match(r'^\s*([0-9a-f]+):\s+(.+)',line)
   if m and start<=int(m[1],16)<stop:rows.append(dict(address=int(m[1],16),asm=m[2]))
  if not rows or rows[0]['address']!=start:raise ValueError('Decode boundary differs')
  return rows
 frame=decode('esprade_01030.prx',0xe80,0xf9b);helper=decode('esprade_01030.prx',0x1020,0x10b2)
 flow=ordered(frame,[r'mov\s+r14, rdx',r'mov\s+r15, rdi',r'call\s+0x2b8\b',r'mov\s+esi, 0x1',r'call\s+0x2b8\b',r'call\s+0x108\b'])
 helperflow=ordered(helper,[r'call\s+0xc8\b',r'call\s+0x118\b',r'call\s+0xe5bf0\b'])
 extra=decode('emu_integration_01500.prx',0x5ca70,0x5ca97)
 core=decode('esprade_01030.prx',0xe5bf0,0xe5d0a)
 digital=decode('emu_integration_01500.prx',0x5cc80,0x5cd45)
 selector=decode('esprade_01030.prx',0x10e0,0x1111)
 selection=ordered(selector,[r'test\s+esi, esi',r'je\s+0x1105\b',r'call\s+0x258\b',r'cmp\s+eax, 0x1',r'mov\s+eax, 0x2',r'sbb\s+eax, 0x0',r'0x3403f60',r'mov\s+dword ptr \[rcx\], eax'])
 thunk=image_bytes(g,0x258,6)
 if thunk[:2]!=b'\xff\x25':raise ValueError('Machine-profile import thunk differs')
 import struct
 got=0x258+6+struct.unpack_from('<i',thunk,2)[0]
 relocation,=[r for r in g.relocations if r['address']==got and r['type']==7]
 name=g.symbols[relocation['symbol']]['name']
 machine,=[s for s in g.inventory()['imports'] if s['name']==name]
 if name.split('#')[0]!='WslcK1FQcGI':raise ValueError('Original machine-profile NID differs')
 ahead_order=ordered(core,[r'0x3403f60',r'call\s+0x788\b',r'call\s+0xe5d10\b',r'call\s+0x788\b',r'call\s+0x788\b',r'call\s+0xe5f30\b'])
 parent=decode('eboot.bin',0x1c5e0,0x1c61b)
 dispatch=decode('eboot.bin',0x28950,0x28a75)
 ordered(parent,[r'call\s+0x28950\b'])
 ordered(dispatch,[r'call\s+qword ptr \[rax \+ 0x50\]',r'jmp\s+rax'])
 report=dict(game='esprade',module_name='esprade_01030.prx',guest_execution=False,original_data_modified=False,descriptor=desc,frame_slot=0x3400088,frame_callback=0xe80,imports=imports,input_before_core_order=flow,helper_order=helperflow,callback_instructions=frame,helper_instructions=helper,core_wrapper_instructions=core,digital_instructions=digital,extra_frames_instructions=extra,extra_frames_setting='console.overdrive',frame_duration_seconds=struct.unpack('<d',image_bytes(provider,0x9ba38,8))[0],core_step_parameter_abi='SysV pointer in RDI, double duration in XMM0',sources=[dict(path=str(path),sha256=digest(path),decoded_sha256=hashlib.sha256(images[n].raw).hexdigest()) for n,path in files.items()],objdump_sha256=digest(objdump),generator_sha256=digest(Path(__file__)),limitations=['Static order does not prove when the emulated CPU consumes the input or which frame first responds','Extra-frame paths are not assumed active without runtime observations'])
 report.update(look_ahead=dict(setting_callback=0x10e0,variable=0x3403f60,
   machine_import=machine,enabled_base_frames=1,enabled_neo_frames=2,
   selector_order=selection,selector_instructions=selector,normal_save_predict_restore_order=ahead_order,
   save=0xe5d10,restore=0xe5f30,prediction_step_seconds=struct.unpack('<d',image_bytes(g,0xf0a98,8))[0],
   distinct_from_console_overdrive=True),original_parent_instructions=parent,
   original_dispatch_instructions=dispatch,callback_tail_parent_return=0x1c60b,
   profile_sources=[dict(path=str(ROOT/n),sha256=digest(ROOT/n)) for n in ('backends/ps4/native_profile.h','backends/ps4/native_profile.cpp')])
 write_json_new(out/'report.json',report);print(json.dumps(dict(output=str(out/'report.json'),frame_slot=hex(report['frame_slot']),frame_callback=hex(report['frame_callback']),imports=imports),indent=2))
if __name__=='__main__':main()
