"""Summarize driver DXGI-to-OS timing, keeping processes and presentation modes separate.

This cannot associate a Vulkan image or input event with an OS display event.
Other/GDI rows retain mode counts but have no inferred DXGI latency.
"""
import argparse
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from present_latency import distribution, number
from project import digest, write_json_new


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path)
    parser.add_argument('--receipt',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--qpc-frequency',type=int,default=10_000_000)
    parser.add_argument('--warmup-ms',type=int,default=1000)
    args=parser.parse_args()
    if args.qpc_frequency<=0 or args.warmup_ms<0 or args.output.exists():
        raise ValueError('Valid clock, warmup and fresh output required')
    receipt=json.loads(args.receipt.read_text(encoding='utf-8-sig'))
    if Path(receipt['output']).resolve()!=args.capture.resolve():
        raise ValueError('Collector receipt/capture path disagree')
    with args.capture.open(encoding='utf-8-sig',newline='') as stream:
        rows=list(csv.DictReader(stream))
    if not rows:raise ValueError('No display events')
    first={}; previous={}; chains=defaultdict(set);out_of_order=Counter();identities=set()
    for r in rows:
        pid=int(r['ProcessID']);qpc=int(r['CPUStartQPC'])
        identity=(pid,r['SwapChainAddress'],r['PresentRuntime'],qpc)
        if identity in identities:raise ValueError('Repeated application frame identity')
        identities.add(identity)
        # ETW can flush startup rows out of order. These are unordered policy
        # distributions, never an inferred frame-to-frame Vulkan association.
        if pid in previous and qpc<previous[pid]:out_of_order[pid]+=1
        previous[pid]=qpc;first[pid]=min(first.get(pid,qpc),qpc)
        chains[pid].add((r['SwapChainAddress'],r['PresentRuntime']))
    groups={}
    for r in rows:
        pid=int(r['ProcessID']);qpc=int(r['CPUStartQPC'])
        key=(r['Application'],pid,r['SwapChainAddress'],r['PresentRuntime'],
             r['PresentMode'],r['SyncInterval'],r['PresentFlags'],r['AllowsTearing'])
        group=groups.setdefault(key,dict(counts=Counter(),tail=[],gpu=[]))
        group['counts']['rows']+=1
        if qpc<first[pid]+args.warmup_ms*args.qpc_frequency/1000:
            group['counts']['warmup_rows']+=1;continue
        group['counts']['steady_rows']+=1
        if r['GPUBusy']!='NA':group['gpu'].append(number(r['GPUBusy'],'GPUBusy'))
        if r['PresentRuntime']!='DXGI':
            group['counts']['not_dxgi_instrumentation']+=1;continue
        busy=number(r['CPUBusy'],'CPUBusy');wait=number(r['CPUWait'],'CPUWait')
        frame=number(r['FrameTime'],'FrameTime')
        if abs(frame-busy-wait)>.00031:raise ValueError('DXGI frame accounting differs')
        if r['DisplayLatency']=='NA':
            group['counts']['display_unobserved_rows']+=1;continue
        tail=number(r['DisplayLatency'],'DisplayLatency')-busy
        if tail<0:raise ValueError('OS display precedes driver Present entry')
        group['tail'].append(tail);group['counts']['accepted_display_rows']+=1
    result=dict(schema=1,collector_version='2.5.1',collector=receipt,
        qpc_frequency=args.qpc_frequency,warmup_ms=args.warmup_ms,rows=len(rows),
        raw_out_of_order_rows=dict(out_of_order),duplicate_frame_identities=0,
        process_chains={str(k):[list(v) for v in sorted(values)] for k,values in chains.items()},
        groups=[dict(application=k[0],pid=k[1],swapchain=k[2],runtime=k[3],mode=k[4],
                     sync_interval=int(k[5]),present_flags=int(k[6]),allows_tearing=int(k[7]),
                     counts=dict(v['counts']),driver_dxgi_entry_to_os_display_ms=distribution(v['tail']),
                     gpu_busy_ms=distribution(v['gpu'])) for k,v in groups.items()],
        scope='Driver-internal DXGI Present entry to OS adoption, not application Vulkan Present or input-to-display',
        formula='DisplayLatency - CPUBusy',
        scene_scope='Mixed startup/menu/attract/gameplay; exact scene and input lineage require separate captures',
        unmeasured=['same_image_application_Vulkan_Present_to_OS','device_input','monitor_scanout','panel_photons'])
    result['sources']=[dict(path=str(p.resolve()),sha256=digest(p)) for p in
                       (Path(__file__),args.capture,args.receipt)]
    write_json_new(args.output,result)
    print(json.dumps(dict(rows=len(rows),processes=len(chains),groups=len(groups),
                         output=str(args.output),sha256=digest(args.output)),indent=2))


if __name__=='__main__':main()
