"""Pair original ESP Ra.De. PadRead batches, core delivery and look-ahead steps.

This stops at callback completion. Response pixels are verified separately.
"""
import argparse,json,struct
from pathlib import Path
from collections import Counter
from latency_probe import read_trace
from project import digest,write_json_new

SITES={99:0x3400088,100:0xe80,101:0xe80,102:0x5cc80,103:0x5cc80,
       104:0x5cc80,110:0x5ca70,112:0x66f60,113:0x66f60,
       114:0x66f60,115:0x66f60,116:0x3403f60}
def f64(bits):return struct.unpack('<d',struct.pack('<Q',bits))[0]

def analyze(trace,first=301,last=304,main_base=0x10000000):
 if not trace['complete']:raise ValueError('Complete original core trace required')
 rows=sorted(trace['records'],key=lambda r:(r['qpc'],r['seq']));by={r['seq']:r for r in rows};hz=trace['frequency']
 if any(SITES.get(r['a'])!=r['b'] for r in rows if r['kind']==10):raise ValueError('Original ESP Ra.De. sites differ')
 latest={};batches={};frames={};polls={};ends={};applies={};delivered={};extra={};settings={};steps={};step_ends={};clocks={}
 for r in rows:
  if r['kind']==18:latest.pop(r['thread'],None)
  elif r['kind']==20:batches.setdefault(r['a'],[]).append(r)
  elif r['kind']==19:
   b=by.get(r['a']);reports=batches.get(r['a'],[])
   if not b or b['kind']!=18 or b['thread']!=r['thread'] or b['e'] or r['b']!=r['c'] or [x['b'] for x in reports]!=list(range(r['c'])):raise ValueError('Original PadRead batch differs')
   if reports:latest[r['thread']]=(b,r,reports[-1])
  elif r['kind']==10:
   site=r['a']
   if site==100:
    frames[r['seq']]=r
    if r['thread'] in latest:polls[r['seq']]=latest.pop(r['thread'])
   elif site==101:ends[r['c']]=r
   elif site==102 and r['d']==0:
    if r['c'] in applies:raise ValueError('Duplicate P1 delivery')
    applies[r['c']]=r
   elif site==103 and r['e']==0:delivered[r['c']]=r
   elif site==110:extra[r['c']]=r
   elif site==116:settings[r['c']]=r
   elif site==112:steps.setdefault(r['c'],[]).append(r)
   elif site==113:step_ends[r['c']]=r
   elif site in (114,115):clocks[(r['c'],site)]=r
 candidates={};samples=[]
 for ident,(b,pad,report) in polls.items():
  source=by.get(report['f'])
  if not source or source['kind']!=14 or source['c']!=1 or not first<=source['a']<=last or source['seq'] in candidates:continue
  candidates[source['seq']]=ident
  frame=frames[ident];apply=applies.get(ident);end=ends.get(ident);setting=settings.get(ident);option=extra.get(ident)
  if not apply or not end or not setting or not option or apply['seq'] not in delivered:raise ValueError('Incomplete first input callback')
  delivery=delivered[apply['seq']]
  expected={8:(0x20,0x100),4:(0x80,0x80)}.get(source['b'])
  if not expected or report['c']!=expected[0] or apply['e']!=((3<<32)|expected[1]):raise ValueError('Last report does not match original P1 digital state')
  if frame['f']!=main_base+0x1c60b:raise ValueError('Not the audited original tail-parent dispatch')
  points=[source,pad,frame,apply,delivery,end]
  if any(x['thread']!=frame['thread'] for x in points[1:]) or [x['qpc'] for x in points]!=sorted(x['qpc'] for x in points):raise ValueError('Input/core order or thread differs')
  calls=steps.get(ident,[])
  if option['d']!=0 or len(calls)!=1+setting['d'] or setting['d'] not in (1,2):raise ValueError('Unexpected extra-frame branch or actual core-step count')
  detail=[]
  for call in calls:
   finished=step_ends.get(call['seq']);before=clocks.get((call['seq'],114));after=clocks.get((call['seq'],115))
   if not finished or not before or not after or finished['d']!=ident or not delivery['qpc']<=call['qpc']<=before['qpc']<=after['qpc']<=finished['qpc']<=end['qpc']:raise ValueError('Core step ownership/order differs')
   detail.append(dict(begin=call['seq'],end=finished['seq'],duration_argument_seconds=f64(call['e']),wall_ms=(finished['qpc']-call['qpc'])*1000/hz,clock_before=[before[k] for k in ('d','e','f')],clock_after=[after[k] for k in ('d','e','f')]))
  samples.append(dict(command=source['a'],source_buttons=source['b'],raw_digital_buttons=expected[1],look_ahead_frames=setting['d'],console_overdrive=option['d'],points=dict(source=source['seq'],pad_return=pad['seq'],last_report=report['seq'],frame_begin=ident,delivery_end=delivery['seq'],frame_end=end['seq']),pad_to_delivery_ms=(delivery['qpc']-pad['qpc'])*1000/hz,callback_wall_ms=(end['qpc']-frame['qpc'])*1000/hz,steps=detail))
 requested=[r['a'] for r in rows if r['kind']==14 and r['c']==1 and first<=r['a']<=last]
 if [s['command'] for s in samples]!=requested:raise ValueError('Missing first original input callback')
 return dict(capture={k:trace[k] for k in ('pid','complete','committed','overflow')},scope='Original LAST PadRead -> P1 integration delivery -> normal and look-ahead core steps; first response not implied',samples=samples,original_callback_count=len(frames),look_ahead_counts=dict(Counter(r['d'] for r in settings.values())))

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('trace',type=Path);p.add_argument('--first',type=int,default=301);p.add_argument('--last',type=int,default=304);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 result=analyze(read_trace(a.trace.read_text()),a.first,a.last);result['sources']=[dict(path=str(x.resolve()),sha256=digest(x)) for x in (a.trace,Path(__file__))];write_json_new(a.output,result)
 print(json.dumps({k:v for k,v in result.items() if k not in ('samples','sources')},indent=2))
 for s in result['samples']:print(s['command'],'ahead',s['look_ahead_frames'],'pad->delivery',round(s['pad_to_delivery_ms'],4),'callback ms',round(s['callback_wall_ms'],4),'step wall ms',[round(v['wall_ms'],4) for v in s['steps']])
if __name__=='__main__':main()
