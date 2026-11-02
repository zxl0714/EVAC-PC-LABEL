"""Correlate an isolated PS2 test-pad command with pad reads and scanout frames."""
import argparse
import csv
import json
from pathlib import Path
import re
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from project import write_json_new


def table(path):
    with path.open(encoding='utf-8', newline='') as file:
        lines = file.readlines()
    return lines[0].strip(), list(csv.DictReader(line for line in lines if not line.startswith('#')))


def png(path, pixels, width, height):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    scan = b''.join(b'\0' + pixels[y*width*4:(y+1)*width*4] for y in range(height))
    with path.open('xb') as file:
        file.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
                   chunk(b'IDAT', zlib.compress(scan, 3)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--player-region',help='Optional x,y,width,height for the green-sprite review aid')
    args = parser.parse_args()
    header, events = table(args.capture/'input.csv')
    if not header.endswith(' overflow=0'):
        raise ValueError('Input timeline is incomplete')
    synthetic_header, synthetic = table(args.capture/'synthetic-input.csv')
    frequency = int(re.search(r'frequency=(\d+)', synthetic_header)[1])
    if not (args.capture/'synthetic-input.csv').read_text().rstrip().endswith('incomplete=0 overflow=0'):
        raise ValueError('Synthetic input timeline is incomplete')
    metadata, frames = table(args.capture/'scanout.csv')
    geometry = {key:int(value) for key,value in re.findall(r'(\w+)=(\d+)',metadata)}
    if geometry['failed'] or geometry['format'] != 37 or geometry['captured'] != len(frames):
        raise ValueError('A complete RGBA8 pixel capture is required')
    width, height, stride = (geometry[key] for key in ('width','height','stride'))
    region=(0,height*3//4,width,height*97//100-height*3//4)
    if args.player_region:
        try:region=tuple(int(value) for value in args.player_region.split(','))
        except ValueError:raise ValueError('Player region must be x,y,width,height')
        if len(region)!=4:raise ValueError('Player region must be x,y,width,height')
    rx,ry,rw,rh=region
    if min(rx,ry)<0 or min(rw,rh)<=0 or rx+rw>width or ry+rh>height:
        raise ValueError('Player region exceeds the captured image')
    pixels = (args.capture/'scanout.bin').read_bytes()
    if len(pixels) != stride*len(frames) or stride != width*height*4:
        raise ValueError('Pixel capture size differs from its metadata')
    args.output.mkdir(parents=True, exist_ok=False)
    reads = [row for row in events if row['kind']=='pad_read' and int(row['a'])==0]
    presents = {int(row['frame']):int(row['qpc_us']) for row in events if row['kind']=='present_end'}
    queues = {int(row['frame']):int(row['qpc_us']) for row in events if row['kind']=='frame_queue'}
    commands=[]
    for row in synthetic:
        if row['kind']!='14' or not 90 <= int(row['a']) < 1000 or row['c']!='1':
            continue
        mask=int(row['b'])
        if mask not in (1,2,4,8):
            continue
        published=int(row['qpc'])*1e6/frequency
        ps2_mask={1:0x10,2:0x40,4:0x80,8:0x20}[mask]
        first=next((r for r in reads if int(r['qpc_us'])>=published and int(r['c']) & ps2_mask),None)
        if first is None:
            raise ValueError('Measurement command was not consumed by the game')
        field=int(first['frame'])
        candidates=[]
        for frame in frames:
            tick=int(frame['field'])
            if not field-4 <= tick <= field+8:
                continue
            index=int(frame['index'])
            data=pixels[index*stride:(index+1)*stride]
            png(args.output/f'command-{row["a"]}-field-{tick}.png',data,width,height)
            # Image-analysis aid only: the low playfield green silhouette can
            # identify movement, but animation/visibility still needs review.
            xs=[]
            for y in range(ry,ry+rh):
                for x in range(rx,rx+rw):
                    at=(y*width+x)*4
                    red,green,blue=data[at:at+3]
                    if green>100 and green-red>45 and green-blue>30:
                        xs.append(x)
            candidates.append(dict(field=tick,green_pixels=len(xs),
                green_x=sum(xs)/len(xs) if xs else None,
                queue_ms=(queues[tick]-published)/1000 if tick in queues else None,
                present_end_ms=(presents[tick]-published)/1000 if tick in presents else None))
        commands.append(dict(id=int(row['a']),published_us=published,pad_us=int(first['qpc_us']),
            host_to_pad_ms=(int(first['qpc_us'])-published)/1000,pad_field=field,candidates=candidates))
    if not commands:
        raise ValueError('No directional measurement command was captured')
    record=dict(geometry=geometry,player_review_region=region,commands=commands,response_frame_requires_image_review=True,
                display_engine_or_photon_latency_measured=False)
    write_json_new(args.output/'analysis.json',record)
    print(json.dumps(record,indent=2))


if __name__=='__main__':
    main()
