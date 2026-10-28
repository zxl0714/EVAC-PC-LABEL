"""Reuse verified private PS4 ROI sources with original Daioujou callback pins."""
import argparse
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from project import digest,write_json_new
from prepare_ps4_native_probe import replace_one


def header(text,module='daioujou_01000.prx'):
    if module not in ('daioujou_01000.prx','daioujou_01030.prx'):
        raise ValueError('Only independently audited Daioujou modules are allowed')
    for before,after in (
        ('ketsui-probe-pins.h','daioujou-probe-pins.h'),
        ('ketsui_core_probe','daioujou_core_probe'),('IsKetsui','IsDaioujou'),
        ('kKetsuiModuleSha','kDaioujouModuleSha'),('kKetsuiIntegrationSha','kDaioujouIntegrationSha'),
        ('ketsui_01000.prx',module),('emu_integration_01400.prx','emu_integration_01700.prx'),
        ('0x50088','0x400088'),('0x6610','0x6700'),('0x5c0a0','0x61250'),
        ('Private Ketsui','Private Daioujou')):
        if before not in text: raise ValueError('Original private observer layout differs: '+before)
        text=text.replace(before,after)
    return text


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base',type=Path,required=True)
    parser.add_argument('--audit',type=Path,required=True)
    parser.add_argument('--destination',type=Path,required=True)
    args=parser.parse_args();base,audit_path,destination=(p.resolve() for p in (args.base,args.audit,args.destination))
    private=ROOT/'.local/input-latency-v080-r1'
    if not all(p.is_relative_to(private) for p in (base,audit_path,destination)) or destination.exists():
        raise ValueError('Private verified sources and fresh destination required')
    audit=json.loads(audit_path.read_text())
    module=audit.get('module_name','daioujou_01000.prx')
    if audit['descriptor']['library']!='lib'+Path(module).stem:
        raise ValueError('Original module descriptor/library identity differs')
    if (audit['game'],audit['frame_slot'],audit['frame_callback'],audit['imports']['deliver_input']['provider_offset'])!=('daioujou',0x400088,0x6700,0x61250):
        raise ValueError('Daioujou original audit differs')
    for source in audit['sources']:
        if digest(Path(source['path']))!=source['sha256']: raise ValueError('Original game changed')
    prior=json.loads((base/'preparation.json').read_text());outputs={}
    for item in prior['files']:
        if digest(Path(item['source']))!=item['original_sha256'] or digest(Path(item['copy']))!=item['copy_sha256']:
            raise ValueError('Private base or original source changed')
        text=Path(item['copy']).read_text(encoding='utf-8');name=Path(item['copy']).name
        if name=='native_probe.cpp':
            text=replace_one(text,'#include "ps4_ketsui_core_probe.h"','#include "ps4_daioujou_core_probe.h"')
            if text.count('ketsui_core_probe::')!=3: raise ValueError('Expected both import bindings and callback install')
            text=text.replace('ketsui_core_probe::','daioujou_core_probe::')
        outputs[name]=(item,text)
    shared=ROOT/'tools/latency/ps4_ketsui_core_probe.h'
    new_header=header(shared.read_text(),module)
    destination.mkdir();files=[]
    for name,(item,text) in outputs.items():
        target=destination/name;target.write_text(text,encoding='utf-8')
        files.append(dict(item,copy=str(target),copy_sha256=digest(target)))
    target=destination/'ps4_daioujou_core_probe.h';target.write_text(new_header,encoding='utf-8')
    files.append(dict(source=str(shared),original_sha256=digest(shared),copy=str(target),copy_sha256=digest(target)))
    hashes={Path(s['path']).name:s['sha256'] for s in audit['sources']}
    def literal(value): return ','.join('0x'+value[i:i+2] for i in range(0,64,2))
    pins=destination/'daioujou-probe-pins.h'
    pins.write_text('#pragma once\n'+''.join(
        f'constexpr std::array<unsigned char,32> {name}{{{literal(hashes[file])}}};\n'
        for name,file in (('kDaioujouModuleSha',module),
                          ('kDaioujouIntegrationSha','emu_integration_01700.prx'))),encoding='ascii')
    files.append(dict(source=str(audit_path),original_sha256=digest(audit_path),copy=str(pins),copy_sha256=digest(pins)))
    write_json_new(destination/'preparation.json',dict(files=files,private_only=True,
        base_preparation_sha256=digest(base/'preparation.json'),audit_sha256=digest(audit_path),
        generator_sha256=digest(Path(__file__)),original_game_instructions_modified=False,
        core_raw_direction_mapping_not_assumed_from_ketsui=True))
    print(destination)


if __name__=='__main__': main()
