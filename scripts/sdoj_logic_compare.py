"""Compare explicitly selected, contiguous logical-frame windows of one replay.

No auto realignment, skipped mismatches, pointer masking or command normalization.
Raw command SHA-256 agreement is an exact byte-content test, not a pixel, audio,
device, scanout or whole-program state equivalence proof. The input fields are
the live port snapshot before the native copy. During playback the native
replay reader supplies gameplay input later; these fields do not capture that
decoded replay input (a zero live snapshot does not mean an idle replay).
"""
import argparse
import json
from pathlib import Path
import re

from project import digest, write_json_new
from sdoj_latency import run_logs

SEMANTIC=('input0','input1','previous0','previous1','depth','command_bytes','command_sha256')
CORE_STATE=('core_state','play_state','end_request')
INTEGER=('module','generation','logic','batch','thread','depth','command_bytes','t_us','limit')


def records(lines):
    result=[]
    for number,line in enumerate(lines,1):
        if 'SDOJ_LOGIC ' not in line: continue
        fields=dict(re.findall(r'(\w+)=([^\s]+)',line))
        needed=set(SEMANTIC)|set(INTEGER)|{'valid','issue'}
        if not needed<=fields.keys(): raise ValueError(f'incomplete logic record at line {number}')
        for key in INTEGER:
            if not fields[key].isdecimal(): raise ValueError(f'invalid {key} on line {number}')
            fields[key]=int(fields[key])
        if any(key in fields for key in CORE_STATE):
            if not all(key in fields for key in CORE_STATE):
                raise ValueError(f'incomplete core state on line {number}')
            for key in CORE_STATE:
                if not re.fullmatch('-?[0-9]+',fields[key]):
                    raise ValueError(f'invalid {key} on line {number}')
                fields[key]=int(fields[key])
        for key in SEMANTIC[:4]:
            if not re.fullmatch('[0-9a-fA-F]{8}',fields[key]):
                raise ValueError(f'invalid {key} on line {number}')
            fields[key]=int(fields[key],16)
        if fields['valid'] not in ('true','false'): raise ValueError(f'invalid status on line {number}')
        fields['valid']=fields['valid']=='true'
        if fields['valid'] and not re.fullmatch('[0-9a-f]{64}',fields['command_sha256']):
            raise ValueError(f'invalid SHA-256 on line {number}')
        result.append(fields|{'line':number})
    return result


def window(rows, *,module,generation,first,frames):
    if module not in (1,2) or generation<1 or first<1 or frames<1:
        raise ValueError('module, generation, first and frames must identify a positive explicit window')
    selected=[row for row in rows if row['module']==module and row['generation']==generation
              and first<=row['logic']<first+frames]
    selected.sort(key=lambda row:row['logic'])
    if [row['logic'] for row in selected]!=list(range(first,first+frames)):
        raise ValueError('missing or duplicate logical frames in the requested window')
    for previous,current in zip(selected,selected[1:]):
        if current['batch']!=previous['batch']+1:
            raise ValueError('non-contiguous command batches in the requested window')
        if current['t_us']<previous['t_us'] or current['thread']!=previous['thread']:
            raise ValueError('regressing time or changing writer in the requested window')
    if any(not row['valid'] or row['issue']!='none' or not 1<=row['depth']<=16
           or not 4<=row['command_bytes']<=0x120000 or row['command_bytes']%4 for row in selected):
        raise ValueError('rejected input/command/completion record in the requested window')
    if any(any(key in row for key in CORE_STATE) and
           not all(key in row and 0<=row[key]<=0xffffffff for key in CORE_STATE) for row in selected):
        raise ValueError('rejected native core state in the requested window')
    return selected


def compare(reference,candidate):
    if not reference or len(reference)!=len(candidate): raise ValueError('equal nonempty windows are required')
    changes=[]; different_frames=0
    has_core_state=any(any(key in row for key in CORE_STATE) for row in reference+candidate)
    if has_core_state and not all(all(key in row for key in CORE_STATE) for row in reference+candidate):
        raise ValueError('cannot mix records with and without native core-state observations')
    fields=SEMANTIC+(CORE_STATE if has_core_state else ())
    for offset,(left,right) in enumerate(zip(reference,candidate)):
        if left['module']!=right['module']:raise ValueError('different gameplay modules cannot be compared')
        differences={key:{'reference':left[key],'candidate':right[key]}
                     for key in fields if left[key]!=right[key]}
        if differences:
            different_frames+=1
            if len(changes)<32:
                changes.append(dict(offset=offset,reference_logic=left['logic'],
                                    candidate_logic=right['logic'],differences=differences))
    return dict(equal=different_frames==0,frames=len(reference),different_frames=different_frames,
                first_differences=changes,fields=list(fields),
                scope='Exact selected pre-copy live input snapshots, raw command bytes by SHA-256 and native slowdown depth; '
                      'not decoded replay inputs, pixels, audio, physical latency or whole-program state equivalence')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference',type=Path); parser.add_argument('candidate',type=Path)
    for name in ('module','reference-generation','candidate-generation','reference-first','candidate-first','frames'):
        parser.add_argument('--'+name,type=int,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    sources=[]; windows=[]
    for name in ('reference','candidate'):
        paths=run_logs(getattr(args,name))
        lines=[line for path in paths for line in path.read_text(encoding='utf-8').splitlines()]
        sources.append(dict(role=name,logs=[dict(path=str(path),sha256=digest(path)) for path in paths]))
        windows.append(window(records(lines),module=args.module,generation=getattr(args,name+'_generation'),
                              first=getattr(args,name+'_first'),frames=args.frames))
    report=compare(*windows)|dict(sources=sources,selection=vars(args)|{'reference':str(args.reference),
                          'candidate':str(args.candidate),'output':str(args.output)})
    write_json_new(args.output,report)
    print(json.dumps({key:report[key] for key in ('equal','frames','different_frames')},indent=2))
    return 0 if report['equal'] else 1


if __name__=='__main__':raise SystemExit(main())
