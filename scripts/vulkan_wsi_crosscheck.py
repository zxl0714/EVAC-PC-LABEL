"""Cross-check the single-producer WSI bench with asynchronous driver DXGI events.

This is NOT a generic game image/ETW matcher. The bench issues one Vulkan present
per numbered work item, one swapchain, and one submission thread. A driver call
must fall wholly between that request and the next request; duplicate/ambiguous
ownership is rejected. Vulkan FIFO/Mailbox feedback is an independent check.
"""
import argparse
from bisect import bisect_right
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path

from present_latency import distribution, number
from project import digest, write_json_new


def crosscheck(text, rows, warmup_ms=1000):
    records = [json.loads(s) for s in text.splitlines() if s.strip()]
    if (not records or records[0].get('kind') != 'header' or
            records[-1].get('kind') != 'end' or records[0].get('schema') != 1):
        raise ValueError('Complete benchmark capture required')
    hz, pid = records[0]['frequency'], records[0]['pid']
    if type(hz) is not int or hz <= 0 or type(pid) is not int or pid <= 0:
        raise ValueError('Invalid benchmark identity/clock')
    calls = [r for r in records if r['kind'] == 'present']
    work = [r for r in records if r['kind'] == 'work']
    chains = [r for r in records if r['kind'] == 'chain']
    if (len(chains) != 1 or not calls or len(work) != len(calls) or
            records[-1]['requests'] != len(calls)):
        raise ValueError('One complete single-producer benchmark chain required')
    if ([r['id'] for r in calls] != list(range(1, len(calls)+1)) or
            [r['id'] for r in work] != [r['id'] for r in calls]):
        raise ValueError('Non-sequential or missing benchmark work')
    generation = chains[0]['generation']
    for call, item in zip(calls, work):
        if (call['generation'] != generation or item['generation'] != generation or
                item['frame'] != call['id']-1 or call['source'] != item['frame'] or
                call['result'] not in (0, 1000001003) or
                not item['begin_qpc'] <= item['acquired_qpc'] <= item['submitted_qpc'] <= call['begin_qpc'] <= call['end_qpc'] <= item['retired_qpc']):
            raise ValueError('Invalid single-producer work/Present contract')
    begins = [r['begin_qpc'] for r in calls]
    if any(b['begin_qpc'] <= a['end_qpc'] for a,b in zip(calls,calls[1:])):
        raise ValueError('Overlapping benchmark requests')
    identities, used, samples = set(), set(), []
    counts = Counter()
    groups = defaultdict(lambda: defaultdict(list))
    previous = None
    for row in rows:
        if int(row['ProcessID']) != pid:
            continue
        counts['etw_rows'] += 1
        identities.add((row['SwapChainAddress'],row['PresentRuntime']))
        if len(identities) > 1 or row['PresentRuntime'] != 'DXGI':
            raise ValueError('One driver DXGI swapchain required')
        if row.get('FrameType','Application') != 'Application':
            raise ValueError('Generated frames require another analysis')
        qpc = int(row['CPUStartQPC'])
        busy, wait, frame = (number(row[k], k) for k in ('CPUBusy','CPUWait','FrameTime'))
        if abs(busy+wait-frame) > .00031:
            raise ValueError('Inconsistent driver frame metrics')
        entry, returned = qpc+busy*hz/1000, qpc+frame*hz/1000
        if previous is not None and entry <= previous:
            raise ValueError('Repeated/reversed driver calls')
        previous = entry
        index = bisect_right(begins, entry)-1
        if not 0 <= index < len(calls)-1:
            counts['outside_bounded_request_interval'] += 1
            continue
        call = calls[index]
        if returned >= calls[index+1]['begin_qpc']:
            raise ValueError('Driver call crosses the next request; ownership ambiguous')
        if index in used:
            raise ValueError('Multiple driver calls for one benchmark work item')
        used.add(index)
        if begins[index] < begins[0]+warmup_ms*hz/1000:
            counts['warmup_excluded'] += 1
            continue
        if row['DisplayLatency'] == 'NA' or row['DisplayedTime'] == 'NA':
            if row['DisplayLatency'] != row['DisplayedTime']:
                raise ValueError('Partial display timing')
            counts['unobserved_display'] += 1
            continue
        if not number(row['DisplayedTime'],'DisplayedTime'):
            counts['zero_display_duration'] += 1
            continue
        display = qpc+number(row['DisplayLatency'],'DisplayLatency')*hz/1000
        if display < call['begin_qpc']:
            raise ValueError('OS display precedes its benchmark request')
        sample = dict(id=call['id'],generation=generation,
                      vulkan_begin_qpc=call['begin_qpc'],driver_entry_qpc=entry,
                      driver_return_qpc=returned,os_display_qpc=display,
                      mode=row['PresentMode'],
                      request_to_display_ms=(display-call['begin_qpc'])*1000/hz,
                      request_to_driver_entry_ms=(entry-call['begin_qpc'])*1000/hz)
        samples.append(sample)
        for name in ('request_to_display_ms','request_to_driver_entry_ms'):
            groups[row['PresentMode']][name].append(sample[name])
    if not samples:
        raise ValueError('No bounded observed benchmark displays')
    counts['mapped_requests'] = len(used)
    counts['observed_samples'] = len(samples)
    return dict(schema_version=1,pid=pid,frequency=hz,counts=dict(counts),samples=samples,
                groups={mode:{name:distribution(v) for name,v in values.items()} for mode,values in groups.items()},
                scope='Single-producer benchmark request intervals to asynchronous driver DXGI/OS events',
                assumptions=['One numbered work item, one submission thread, one swapchain and one driver call per request interval.',
                             'Interval ownership is a benchmark constraint, not a causal matcher for arbitrary game images.'],
                limitations=['OS display adoption is not physical panel emission.',
                             'Immediate driver FIRST_PIXEL_OUT feedback is not accepted when it predates its request.',
                             'Do not use this tool for generic game input-to-image lineage.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bench',type=Path,required=True)
    parser.add_argument('--presentmon',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    with args.presentmon.open(encoding='utf-8-sig',newline='') as file:
        result=crosscheck(args.bench.read_text(encoding='utf8'),csv.DictReader(file))
    result['sources']=[dict(path=str(p.resolve()),sha256=digest(p)) for p in (args.bench,args.presentmon)]
    write_json_new(args.output,result)
    print(json.dumps(result['groups'],indent=2))


if __name__=='__main__':main()
