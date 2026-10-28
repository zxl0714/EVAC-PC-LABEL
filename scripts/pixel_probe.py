"""Read bounded D3D12 ROI evidence; frame identity is not a response assertion."""
import argparse
import csv
from io import StringIO
import json
from pathlib import Path
import re


def fields(line):
    return {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', line)}


def read_capture(prefix):
    metadata = Path(str(prefix) + '.csv').read_text(encoding='ascii').splitlines()
    if len(metadata) < 3 or not metadata[0].startswith('# evac_pixels_v1 ') or not metadata[-1].startswith('# end '):
        raise ValueError('Missing pixel capture header/footer')
    header, footer = fields(metadata[0]), fields(metadata[-1])
    required = {'frequency', 'pid', 'width', 'height', 'x', 'y', 'roi_width', 'roi_height', 'format', 'pitch', 'stride', 'capacity'}
    if header.keys() != required or footer.keys() != {'captured', 'valid', 'overflow', 'failed', 'bytes_ok'}:
        raise ValueError('Unexpected pixel metadata')
    if footer['failed'] or not footer['bytes_ok'] or footer['captured'] != footer['valid']:
        raise ValueError('Incomplete pixel capture')
    if not header['frequency'] or header['format'] not in (24, 28, 87):
        raise ValueError('Unsupported pixel clock/format')
    if (header['x'] + header['roi_width'] > header['width'] or
        header['y'] + header['roi_height'] > header['height'] or
        header['pitch'] < 4 * header['roi_width'] or header['pitch'] % 256 or
        header['stride'] < header['pitch'] * header['roi_height'] or header['stride'] % 512):
        raise ValueError('Invalid pixel layout')
    frames = [{k: int(v) for k, v in row.items()}
              for row in csv.DictReader(StringIO('\n'.join(metadata[1:-1])))]
    if len(frames) != footer['valid'] or [f['index'] for f in frames] != list(range(len(frames))):
        raise ValueError('Missing/duplicate pixel frames')
    previous = previous_present = 0
    for frame in frames:
        if (not previous <= frame['copy_qpc'] <= frame['present_begin'] <= frame['present_end'] or
                previous_present > frame['present_begin']):
            raise ValueError('Invalid pixel event order')
        previous, previous_present = frame['copy_qpc'], frame['present_begin']
    binary = Path(str(prefix) + '.bin')
    if binary.stat().st_size != header['stride'] * len(frames):
        raise ValueError('Pixel data size mismatch')
    return dict(header=header, footer=footer, frames=frames, binary=binary)


def rgb(capture, index):
    import numpy as np
    from PIL import Image
    h = capture['header']
    with capture['binary'].open('rb') as stream:
        stream.seek(index * h['stride'])
        block = stream.read(h['pitch'] * h['roi_height'])
    rows = np.frombuffer(block, np.uint8).reshape(h['roi_height'], h['pitch'])
    data = rows[:, :4*h['roi_width']].copy().reshape(h['roi_height'], h['roi_width'], 4)
    if h['format'] == 24:
        words = data.reshape(-1, 4).view('<u4').reshape(h['roi_height'], h['roi_width'])
        pixels = np.stack([((words >> shift) & 1023) * 255 // 1023 for shift in (0, 10, 20)], axis=-1).astype(np.uint8)
    else:
        pixels = data[:, :, [2, 1, 0] if h['format'] == 87 else [0, 1, 2]]
    return Image.fromarray(pixels)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', type=Path)
    parser.add_argument('--sheet', type=Path)
    parser.add_argument('--indices', help='Comma-separated captured frame indexes')
    parser.add_argument('--columns', type=int, choices=range(1,6), default=5)
    args = parser.parse_args()
    capture = read_capture(args.prefix)
    print(json.dumps({k: capture[k] for k in ('header', 'footer')}, indent=2))
    if args.sheet:
        from PIL import Image, ImageDraw
        indices = list(map(int, args.indices.split(','))) if args.indices else list(range(0, len(capture['frames']), 30))[:20]
        if not indices or min(indices) < 0 or max(indices) >= len(capture['frames']) or len(indices) > 60:
            raise ValueError('Invalid contact sheet selection')
        if args.sheet.exists(): raise ValueError('Output already exists')
        width, height = capture['header']['roi_width'], capture['header']['roi_height']
        columns = min(args.columns, len(indices))
        sheet = Image.new('RGB', (width*columns, (height+24)*((len(indices)+columns-1)//columns)), '#202020')
        draw = ImageDraw.Draw(sheet)
        for position, index in enumerate(indices):
            x, y = (position%columns)*width, (position//columns)*(height+24)
            sheet.paste(rgb(capture, index), (x, y+24))
            draw.text((x+4, y+4), f'frame {index}', fill='white')
        sheet.save(args.sheet)


if __name__ == '__main__': main()
