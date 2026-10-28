"""Reuse verified shad ROI observations with independently audited Daioujou pins."""
import argparse
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from project import digest,write_json_new


def transform(text,got):
    for before,after in (
        ('shad_ketsui_core_probe.h','shad_daioujou_core_probe.h'),
        ('ketsui_core_probe','daioujou_core_probe'),
        ('ketsui_01000.prx','daioujou_01030.prx'),
        ('emu_integration_01400.prx','emu_integration_01700.prx'),
        ('0x50088','0x400088'),('0x6610','0x6700'),('0x5c0a0','0x61250'),
        ('0x29629','0x1b55e'),('+313936','+'+str(got)),('shad Ketsui','shad Daioujou')):
        text=text.replace(before,after)
    return text


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base',type=Path,required=True);parser.add_argument('--audit',type=Path,required=True)
    parser.add_argument('--destination',type=Path,required=True)
    args=parser.parse_args();base,audit_path,destination=(p.resolve() for p in (args.base,args.audit,args.destination))
    private=ROOT/'.local/input-latency-v080-r1'
    if not all(p.is_relative_to(private) for p in (base,audit_path,destination)) or destination.exists():
        raise ValueError('Verified private sources and fresh output required')
    audit=json.loads(audit_path.read_text())
    if (audit['game'],audit['module_name'],audit['frame_slot'],audit['frame_callback'],audit['tail_callback_return'])!=('daioujou','daioujou_01030.prx',0x400088,0x6700,0x1b55e):
        raise ValueError('Actual Daioujou ARCADE module and tail caller audit required')
    for item in audit['sources']:
        if digest(Path(item['path']))!=item['sha256']:raise ValueError('Original game changed')
    prior=json.loads((base/'preparation.json').read_text());outputs=[]
    got=audit['imports']['deliver_input']['got']
    if audit['imports']['deliver_input']['provider_offset']!=0x61250:raise ValueError('Different integration provider')
    for item in prior['files']:
        if digest(Path(item['source']))!=item['original_sha256'] or digest(Path(item['copy']))!=item['copy_sha256']:
            raise ValueError('Verified shad source/ROI base changed')
        name=Path(item['copy']).name;text=Path(item['copy']).read_text(encoding='utf-8')
        if name in ('linker.cpp','shad_ketsui_core_probe.h'):
            if text.count('+313936')!=1:raise ValueError('Original exact GOT guard differs')
            text=transform(text,got)
            if 'ketsui_core_probe' in text or '0x5c0a0' in text:raise ValueError('Old core pins remain')
            name=name.replace('shad_ketsui','shad_daioujou')
        outputs.append((item,name,text))
    destination.mkdir();files=[]
    for item,name,text in outputs:
        output=destination/name;output.write_text(text,encoding='utf-8')
        files.append(dict(item,copy=str(output),copy_sha256=digest(output)))
    write_json_new(destination/'preparation.json',dict(files=files,private_only=True,
        game='daioujou',audit_path=str(audit_path),audit_sha256=digest(audit_path),
        base_preparation_sha256=digest(base/'preparation.json'),generator_sha256=digest(Path(__file__)),
        guest_instruction_rewrite=False,added_gameplay_waits=False,
        pixel_ready_is_submission_not_GPU_completion=True))
    print(destination)


if __name__=='__main__':main()
