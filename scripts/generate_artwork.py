"""Verify promotional UI artwork and emit native resource metadata."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from generate_catalogue import load, read_json, relative


def artwork(root):
    root=Path(root).resolve()
    manifest=root/'launcher/artwork.json'
    if not manifest.exists():
        return []
    value=read_json(manifest)
    if type(value['format']) is not int or value['format']!=1:
        raise ValueError('Unsupported artwork manifest')
    entries=value['assets']
    games=[game['id'] for game in load(root/'games/catalogue.json')]
    if [entry['game'] for entry in entries]!=games:
        raise ValueError('Each catalogue game needs one authored banner')
    paths,resources=set(),set()
    for entry in entries:
        name=relative(entry['path'])
        path=root/name
        if (not name.startswith('launcher/assets/') or path.suffix not in ('.jpg','.png') or
                name in paths or entry['resource'] in resources or type(entry['resource']) is not int or
                not 100<=entry['resource']<1000 or type(entry['focusY']) not in (int,float) or not 0<=entry['focusY']<=1):
            raise ValueError('Invalid artwork identity or crop')
        if entry.get('fit', 'cover') not in ('cover', 'contain'):
            raise ValueError('Invalid artwork fit')
        paths.add(name);resources.add(entry['resource'])
        for part in (path,*path.parents):
            if part.is_symlink() or part.is_junction():
                raise ValueError('Linked artwork source')
            if part==root:
                break
        if not path.is_file() or not 1<=path.stat().st_size<=5*1024*1024:
            raise ValueError('Invalid artwork file bounds')
        data=path.read_bytes()
        if not data.startswith((b'\xff\xd8\xff',b'\x89PNG\r\n\x1a\n')):
            raise ValueError('Artwork is not a JPEG or PNG')
        if not re.fullmatch('[0-9a-f]{64}',entry['sha256']) or hashlib.sha256(data).hexdigest()!=entry['sha256']:
            raise ValueError('Artwork differs from recorded source')
        for key in ('sourcePage','sourceUrl'):
            if not entry[key].startswith('https://') or any(c.isspace() for c in entry[key]):
                raise ValueError('Record an HTTPS artwork origin')
        for key in ('kicker','copyright','usage'):
            if not entry[key] or any(ord(c)<32 for c in entry[key]):
                raise ValueError('Missing artwork metadata')
    return entries


def generate(root, destination):
    entries=artwork(root)
    if not entries:
        raise ValueError('No artwork manifest')
    destination=Path(destination)
    destination.mkdir(parents=True,exist_ok=True)
    quoted=lambda text:json.dumps(text,ensure_ascii=False)
    header=['#pragma once','#include <array>','#include <string_view>','namespace cave::launcher {',
            'struct Artwork {std::string_view game,kicker; unsigned resource; float focus_y; bool contain;};',
            'inline constexpr std::array<Artwork,'+str(len(entries))+'> kArtwork{{']
    resources=['#pragma code_page(65001)','#include <windows.h>']
    for entry in entries:
        header.append('{'+quoted(entry['game'])+','+quoted(entry['kicker'])+','+str(entry['resource'])+
                      ',float('+str(entry['focusY'])+'),'+str(entry.get('fit')=='contain').lower()+'},')
        resources.append(str(entry['resource'])+' RCDATA '+quoted((Path(root)/entry['path']).resolve().as_posix()))
    header+=['}};','}']
    for name,lines in [('artwork.h',header),('artwork.rc',resources)]:
        path=destination/name
        text='\n'.join(lines)+'\n'
        if not path.exists() or path.read_text(encoding='utf-8')!=text:
            path.write_text(text,encoding='utf-8',newline='\n')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',type=Path)
    parser.add_argument('destination',type=Path)
    args=parser.parse_args()
    generate(args.root,args.destination)
