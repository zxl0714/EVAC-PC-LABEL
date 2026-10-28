"""Compare same-build Daioujou ROI control; do not infer OFF response pixels."""
import argparse
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from daioujou_core_latency import analyze
from latency_probe import read_trace
from project import digest,write_json_new
from ps4_latency import summarize
from shad_ketsui_core_latency import attach_sources


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend',choices=('evac','shad'),required=True)
    parser.add_argument('--on',required=True);parser.add_argument('--off',required=True)
    parser.add_argument('--on-core',type=Path,required=True);parser.add_argument('--off-core',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();base=ROOT/'.local/input-latency-v080-r1';launches=[];reports=[];sources=[Path(__file__)]
    if not args.output.resolve().is_relative_to(base):raise ValueError('Private output required')
    for label,chain_path,pixels in ((args.on,args.on_core,True),(args.off,args.off_core,False)):
        if Path(label).name!=label or not label.replace('-','').isalnum():raise ValueError('ASCII label required')
        trace_path,launch_path=(base/(label+s) for s in ('-trace.csv','-launch.json'))
        chain=json.loads(chain_path.read_text());launch=json.loads(launch_path.read_text());launches.append(launch)
        for item in chain['sources']:
            if digest(Path(item['path']))!=item['sha256']:raise ValueError('Original core evidence changed')
        trace=read_trace(trace_path.read_text())
        if not trace['complete'] or trace['pid']!=launch['pid'] or bool(launch['environment'].get('EVAC_LATENCY_PIXELS'))!=pixels:
            raise ValueError('Complete matching capture/pixel state required')
        core_trace=trace
        if args.backend=='shad':
            core_trace=attach_sources(trace)
            core_trace=dict(core_trace,records=[r for r in core_trace['records'] if not (r['kind']==10 and r['a']==98)])
        current=analyze(core_trace,chain['main_image_base'],{0x27c04,0x1b55e})
        if current['samples']!=chain['samples'] or current['paired_sources']!=12:
            raise ValueError('Same twelve original deliveries required')
        output=summarize(trace,101,112)
        reports.append(dict(label=label,pid=trace['pid'],events=trace['committed'],pixels_on=pixels,
            core_statistics=current['statistics'],measured_output_images=output['measured_output_images'],
            presentation_statistics=output['statistics']))
        sources += [trace_path,launch_path,chain_path]
    for key in ('command','executable_sha256','config_sha256','stage_sha256'):
        if launches[0][key]!=launches[1][key]:raise ValueError('Build/config/contract differs: '+key)
    result=dict(game='daioujou',backend=args.backend,same_build_config_contract=True,runs=reports,
        scope='Original delivery and owned prepare/handoff/Present timing during twelve requested inputs, not OFF first response',
        off_first_response_not_measured=True,
        conclusion='Compare the recorded distributions; similar tails do not prove zero observer cost',
        sources=[dict(path=str(p.resolve()),sha256=digest(p)) for p in sources])
    write_json_new(args.output,result)
    print(json.dumps(dict(sha256=digest(args.output),runs=[dict(label=r['label'],images=r['measured_output_images'],
        statistics=r['presentation_statistics']) for r in reports]),indent=2))


if __name__=='__main__':main()
