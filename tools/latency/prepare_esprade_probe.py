"""Add audited ESP Ra.De. core observations to verified private GPU/host copies."""
import argparse,json,sys,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path[:0]=[str(ROOT/'scripts'),str(Path(__file__).parent)]
from project import digest,write_json_new
from prepare_ps4_native_probe import replace_one
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--audit',type=Path,required=True);p.add_argument('--destination',type=Path,required=True);a=p.parse_args()
 base,audit,dest=[v.resolve() for v in (a.base,a.audit,a.destination)];private=ROOT/'.local/input-latency-v080-r1'
 if not all(v.is_relative_to(private) for v in (base,audit,dest)) or dest.exists():raise ValueError('Verified private base/audit and fresh destination required')
 report=json.loads(audit.read_text());prior=json.loads((base/'preparation.json').read_text())
 if (report['game'],report['frame_slot'],report['frame_callback'],report['imports']['core_step']['provider_offset'])!=('esprade',0x3400088,0xe80,0x66f60):raise ValueError('Core audit differs')
 for source in report['sources']:
  if digest(Path(source['path']))!=source['sha256']:raise ValueError('Original data changed')
 dest.mkdir();files=[]
 for item in prior['files']:
  if digest(Path(item['source']))!=item['original_sha256'] or digest(Path(item['copy']))!=item['copy_sha256']:raise ValueError('Original/private source changed')
  target=dest/Path(item['copy']).name;shutil.copy2(item['copy'],target)
  if target.name=='native_probe.cpp':
   text=replace_one(target.read_text(),'#include "ps4_ketsui_core_probe.h"','#include "ps4_esprade_core_probe.h"')
   if text.count('ketsui_core_probe::')!=3:raise ValueError('Expected both import bindings and callback install')
   target.write_text(text.replace('ketsui_core_probe::','esprade_core_probe::'),encoding='utf-8')
  files.append(dict(item,copy=str(target),copy_sha256=digest(target)))
 h=ROOT/'tools/latency/ps4_esprade_core_probe.h';shutil.copy2(h,dest/h.name)
 files.append(dict(source=str(h),original_sha256=digest(h),copy=str(dest/h.name),copy_sha256=digest(dest/h.name)))
 hashes={Path(s['path']).name:s['sha256'] for s in report['sources']};pins=dest/'esprade-probe-pins.h'
 pins.write_text('#pragma once\n'+''.join(f'constexpr std::array<unsigned char,32> {name}{{'+','.join('0x'+hashes[file][i:i+2] for i in range(0,64,2))+'};\n' for name,file in [('kEspradeModuleSha','esprade_01030.prx'),('kEspradeIntegrationSha','emu_integration_01500.prx')]),encoding='ascii')
 files.append(dict(source=str(audit),original_sha256=digest(audit),copy=str(pins),copy_sha256=digest(pins)))
 write_json_new(dest/'preparation.json',dict(files=files,private_only=True,audit_sha256=digest(audit),base_preparation_sha256=digest(base/'preparation.json'),generator_sha256=digest(Path(__file__)),original_game_instructions_modified=False))
 print(dest)
if __name__=='__main__':main()
