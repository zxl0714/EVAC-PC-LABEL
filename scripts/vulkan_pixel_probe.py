"""Read private Vulkan snapshot ROI evidence and validate exact trace identities."""
import argparse
import csv
from io import StringIO
import json
from pathlib import Path

from latency_probe import read_trace
from pixel_probe import fields


def read_capture(prefix, trace):
    metadata = Path(str(prefix) + '.csv').read_text(encoding='ascii').splitlines()
    if len(metadata) < 3 or not metadata[0].startswith('# evac_vk_pixels_v1 ') or not metadata[-1].startswith('# end '):
        raise ValueError('Missing Vulkan pixel header/footer')
    header, footer = fields(metadata[0]), fields(metadata[-1])
    required = {'frequency','pid','width','height','x','y','roi_width','roi_height','format','pitch','stride','capacity'}
    if header.keys() != required or footer.keys() != {'captured','valid','overflow','failed','bytes_ok'}:
        raise ValueError('Unexpected Vulkan pixel metadata')
    if footer['failed'] or footer['overflow'] or not footer['bytes_ok'] or footer['captured'] != footer['valid']:
        raise ValueError('Incomplete or overflowing Vulkan pixel capture')
    if not trace['complete'] or (header['frequency'],header['pid']) != (trace['frequency'],trace['pid']):
        raise ValueError('Vulkan pixel trace clock/process identity differs')
    if (header['format'] not in (43,50) or not header['roi_width'] or not header['roi_height'] or
        header['x']+header['roi_width'] > header['width'] or header['y']+header['roi_height'] > header['height'] or
        header['pitch'] != 4*header['roi_width'] or header['stride'] < header['pitch']*header['roi_height'] or
        header['stride'] % 256 or not footer['valid'] <= header['capacity'] <= 1024):
        raise ValueError('Invalid Vulkan pixel layout')
    frames = [{k:int(v) for k,v in row.items()} for row in csv.DictReader(StringIO('\n'.join(metadata[1:-1])))]
    if len(frames) != footer['valid'] or [f['index'] for f in frames] != list(range(len(frames))):
        raise ValueError('Missing/duplicate Vulkan pixel frame')
    by_id={r['seq']:r for r in trace['records']}
    ready_for_prepare={};first_present={}
    for row in sorted(trace['records'],key=lambda r:(r['qpc'],r['seq'])):
        if row['kind']==21:
            if row['a'] in ready_for_prepare: raise ValueError('Duplicate readiness for one prepared image')
            ready_for_prepare[row['a']]=row['seq']
        elif row['kind']==22 and row.get('e',1) and not row.get('f',0):
            first_present.setdefault(row['a'],row['seq'])
    used=set();previous=0;previous_present=0
    for frame in frames:
        prepare=by_id.get(frame['prepare'])
        if (not prepare or prepare['kind']!=26 or frame['prepare'] in used or
            prepare['b']!=header['width'] or prepare['c']!=header['height'] or
            not previous <= prepare['qpc'] <= frame['copy_qpc']):
            raise ValueError('Vulkan pixel prepare identity/order differs')
        used.add(frame['prepare']);previous=frame['copy_qpc']
        if frame['ready']!=ready_for_prepare.get(frame['prepare'],0):
            raise ValueError('Pixel metadata omitted/changed actual image readiness')
        if frame['present_begin']!=first_present.get(frame['ready'],0):
            raise ValueError('Pixel metadata omitted/changed first actual image Present')
        ready=by_id.get(frame['ready']) if frame['ready'] else None
        if ready and (ready['kind']!=21 or ready['a']!=frame['prepare'] or ready['c']!=prepare['a'] or
                      ready['qpc']<frame['copy_qpc'] or ready['thread']!=prepare['thread']):
            raise ValueError('Vulkan pixel ready ownership differs')
        if frame['ready'] and not ready:
            raise ValueError('Missing Vulkan pixel ready')
        if frame['present_begin'] or frame['present_end']:
            begin,end=(by_id.get(frame[name]) for name in ('present_begin','present_end'))
            if (not ready or not begin or not end or begin['kind']!=22 or end['kind']!=23 or
                begin['a']!=ready['seq'] or end['a']!=begin['seq'] or end['b']!=ready['seq'] or
                end['c']!=frame['result'] or begin['thread']!=end['thread'] or
                not ready['qpc']<=begin['qpc']<=end['qpc'] or previous_present>begin['qpc']):
                raise ValueError('Vulkan pixel Present ownership/order differs')
            previous_present=begin['qpc']
        elif frame['result']:
            raise ValueError('Vulkan result without a Present')
    binary=Path(str(prefix)+'.bin')
    if binary.stat().st_size != header['stride']*len(frames):
        raise ValueError('Vulkan pixel binary length differs')
    return dict(header=header,footer=footer,frames=frames,binary=binary)


def rgb(capture,index):
    import numpy as np
    from PIL import Image
    h=capture['header']
    with capture['binary'].open('rb') as stream:
        stream.seek(index*h['stride']);block=stream.read(h['pitch']*h['roi_height'])
    pixels=np.frombuffer(block,np.uint8).reshape(h['roi_height'],h['roi_width'],4)
    return Image.fromarray(pixels[:,:,[2,1,0] if h['format']==50 else [0,1,2]])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix',type=Path)
    parser.add_argument('--trace',type=Path,required=True)
    parser.add_argument('--sheet',type=Path)
    parser.add_argument('--indices')
    parser.add_argument('--columns',type=int,choices=range(1,6),default=5)
    parser.add_argument('--crop',help='Scientific contact-sheet crop within the raw ROI: x,y,width,height')
    args=parser.parse_args()
    capture=read_capture(args.prefix,read_trace(args.trace.read_text()))
    print(json.dumps({k:capture[k] for k in ('header','footer')},indent=2))
    if args.sheet:
        from PIL import Image,ImageDraw
        indices=list(map(int,args.indices.split(','))) if args.indices else list(range(0,len(capture['frames']),15))[:20]
        if not indices or min(indices)<0 or max(indices)>=len(capture['frames']) or len(indices)>60 or args.sheet.exists():
            raise ValueError('Fresh output and bounded frame selection required')
        width,height=(capture['header'][k] for k in ('roi_width','roi_height'))
        crop=(0,0,width,height)
        if args.crop:
            x,y,w,h=map(int,args.crop.split(','))
            if min(x,y)<0 or min(w,h)<=0 or x+w>width or y+h>height:
                raise ValueError('Crop must remain inside the raw GPU ROI')
            crop=(x,y,x+w,y+h);width,height=w,h
        columns=min(args.columns,len(indices))
        sheet=Image.new('RGB',(width*columns,(height+24)*((len(indices)+columns-1)//columns)),'#202020')
        draw=ImageDraw.Draw(sheet)
        for position,index in enumerate(indices):
            x,y=(position%columns)*width,(position//columns)*(height+24)
            sheet.paste(rgb(capture,index).crop(crop),(x,y+24));draw.text((x+4,y+4),f'frame {index}',fill='white')
        sheet.save(args.sheet)


if __name__=='__main__': main()
