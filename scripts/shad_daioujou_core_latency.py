"""Resolve shad's original report history before Daioujou tail-callback pairing."""
import argparse
import json
from pathlib import Path

from daioujou_core_latency import analyze
from latency_probe import read_trace
from project import digest,write_json_new
from shad_ketsui_core_latency import attach_sources


def analyze_shad(trace):
    bases={r['c'] for r in trace['records'] if r['kind']==10 and r['a']==98 and r['b']==0x1b55e}
    if len(bases)!=1:raise ValueError('Unique original Daioujou main image base required')
    base,=bases;attached=attach_sources(trace)
    core=dict(attached,records=[r for r in attached['records'] if not (r['kind']==10 and r['a']==98)])
    result=analyze(core,base,{0x27c04,0x1b55e})
    result['main_image_base']=base
    result['scope']='Original shad sampled-controller queue -> LAST PadRead report -> first original Daioujou callback6700 / integration61250 return; no response image implied'
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path);parser.add_argument('--launch',type=Path,required=True)
    parser.add_argument('--audit',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();launch=json.loads(args.launch.read_text());audit=json.loads(args.audit.read_text())
    if (audit['game'],audit['module_name'],audit['tail_callback_return'])!=('daioujou','daioujou_01030.prx',0x1b55e):
        raise ValueError('Actual game/module/tail-caller audit required')
    if launch['original_audit_sha256']!=digest(args.audit) or launch['audited_original_sources']!=audit['sources']:
        raise ValueError('Launch original identities differ from audit')
    for item in audit['sources']:
        if digest(Path(item['path']))!=item['sha256']:raise ValueError('Original game identity changed')
    trace=read_trace(args.trace.read_text())
    if trace['pid']!=launch['pid']:raise ValueError('Different launch process')
    result=analyze_shad(trace)
    result['sources']=[dict(path=str(p.resolve()),sha256=digest(p)) for p in
        (Path(__file__),Path(__file__).with_name('daioujou_core_latency.py'),
         Path(__file__).with_name('shad_ketsui_core_latency.py'),args.trace,args.launch,args.audit)]
    write_json_new(args.output,result)
    print(json.dumps({k:v for k,v in result.items() if k!='samples'},indent=2))


if __name__=='__main__':main()
