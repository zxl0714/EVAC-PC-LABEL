"""Bounded same-build shad ROI ON/OFF control, not unseen response inference."""
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from ketsui_core_latency import analyze
from latency_probe import read_trace
from project import digest,write_json_new
from ps4_latency import summarize
from shad_ketsui_core_latency import attach_sources


def main():
    base=ROOT/'.local/input-latency-v080-r1';receipts=[];reports=[];sources=[Path(__file__)]
    for label,chain_name,pixels in (
        ('shad-ketsui-core-pixels-r3','shad-ketsui-core-input-r3.json',True),
        ('shad-ketsui-off-r4','shad-ketsui-core-input-off-r4.json',False)):
        trace_path,launch_path=(base/(label+suffix) for suffix in ('-trace.csv','-launch.json'))
        chain_path=base/chain_name;chain=json.loads(chain_path.read_text());launch=json.loads(launch_path.read_text())
        for source in chain['sources']:
            if digest(Path(source['path']))!=source['sha256']: raise ValueError('Original input evidence changed')
        trace=read_trace(trace_path.read_text())
        if trace['pid']!=launch['pid'] or bool(launch['environment'].get('EVAC_LATENCY_PIXELS'))!=pixels:
            raise ValueError('Launch or pixel state differs')
        current=analyze(attach_sources(trace),main_base=chain['main_image_base'])
        if current['samples']!=chain['samples'] or current['paired_sources']!=12:
            raise ValueError('Same twelve original input deliveries required')
        output=summarize(trace,101,112)
        reports.append(dict(label=label,pixels_on=pixels,pid=trace['pid'],events=trace['committed'],
            original_input_deliveries=12,core_statistics=current['statistics'],
            measured_output_images=output['measured_output_images'],presentation_statistics=output['statistics']))
        receipts.append(launch);sources += [trace_path,launch_path,chain_path]
    for key in ('command','executable_sha256','config_sha256','stage_sha256'):
        if receipts[0][key]!=receipts[1][key]: raise ValueError('Not same build/config/contract: '+key)
    result=dict(scope='Same build/config, ARCADE Type-A Stage1 live after bounded sequence; original delivery and owned presentation timing control',
        runs=reports,first_response_pixels_on_only=True,off_first_response_not_measured=True,
        post_sequence_observation='Both immediate CU observations: live Type-A in Stage1, score300; then normal close',
        conclusion='No new whole output cycle is established by the ROI copy; not zero observer cost or an OFF first-response measurement',
        sources=[dict(path=str(p),sha256=digest(p)) for p in sources])
    output=base/'shad-ketsui-pixel-control-r1.json';write_json_new(output,result)
    print(json.dumps(dict(sha256=digest(output),runs=[dict(label=r['label'],
        pad_to_delivery=r['core_statistics']['pad_return_to_delivery_end_us'],
        prepare_to_present=r['presentation_statistics']['prepare_to_present_us']) for r in reports]),indent=2))


if __name__=='__main__': main()
