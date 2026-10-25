"""Verify promotional UI artwork and emit native resource metadata."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from generate_catalogue import load, read_json, relative
from product import NAME, LAUNCHER, VERSION


def png_bounds(data):
    if (len(data)<33 or not data.startswith(b'\x89PNG\r\n\x1a\n') or
            data[8:16]!=b'\x00\x00\x00\rIHDR' or data[24:26]!=b'\x08\x06'):
        raise ValueError('Brand PNG must have an 8-bit RGBA channel')
    width,height=struct.unpack_from('>II',data,16)
    if not 1<=width<=4096 or not 1<=height<=4096:
        raise ValueError('Invalid brand PNG bounds')
    return width,height


def branding(root):
    root=Path(root).resolve()
    manifest=root/'launcher/branding.json'
    if not manifest.exists():
        return []
    value=read_json(manifest)
    if type(value.get('format')) is not int or value['format']!=1 or not value.get('origin'):
        raise ValueError('Invalid branding manifest')
    entries=value['assets']
    expected={'logo':'launcher/branding/evac-logo.png','iconImage':'launcher/branding/evac-icon.png',
              'icon':'launcher/branding/evac.ico'}
    if [entry['name'] for entry in entries]!=list(expected):
        raise ValueError('Branding assets must contain the logo, icon image and icon')
    for entry in entries:
        name=relative(entry['path']);path=root/name
        if name!=expected[entry['name']]:
            raise ValueError('Unexpected branding asset')
        for part in (path,*path.parents):
            if part.is_symlink() or part.is_junction():
                raise ValueError('Linked branding source')
            if part==root:
                break
        if not path.is_file() or not 1<=path.stat().st_size<=5*1024*1024:
            raise ValueError('Invalid branding file bounds')
        data=path.read_bytes()
        if not re.fullmatch('[0-9a-f]{64}',entry['sha256']) or hashlib.sha256(data).hexdigest()!=entry['sha256']:
            raise ValueError('Branding differs from recorded source')
        if entry['name']!='icon':
            width,height=png_bounds(data)
            if entry['name']=='iconImage' and width!=height:
                raise ValueError('Icon image must be square')
            continue
        sizes=(16,20,24,32,40,48,64,128,256)
        if data[:6]!=struct.pack('<HHH',0,1,len(sizes)):
            raise ValueError('Invalid ICO directory')
        offset=6+16*len(sizes)
        if len(data)<offset:
            raise ValueError('Truncated ICO directory')
        for index,size in enumerate(sizes):
            width,height,colors,reserved,planes,bits,length,start=struct.unpack_from('<BBBBHHII',data,6+16*index)
            if ((width or 256,height or 256,colors,reserved,planes,bits)!=(size,size,0,0,1,32) or
                    start!=offset or not 33<=length<=len(data)-offset or
                    png_bounds(data[offset:offset+length])!=(size,size)):
                raise ValueError('Invalid ICO frame')
            offset+=length
        if offset!=len(data):
            raise ValueError('Unexpected ICO trailing content')
    return entries


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


def windows_version(version):
    if not re.fullmatch(r'[0-9]+(?:\.[0-9]+){1,3}', version):
        raise ValueError('Windows version needs two to four numeric components')
    parts = [int(part) for part in version.split('.')]
    if any(part > 65535 for part in parts):
        raise ValueError('Windows version component exceeds 65535')
    return parts + [0] * (4 - len(parts))


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
    version=windows_version(VERSION)
    numeric_version=','.join(map(str,version))
    file_version='.'.join(map(str,version))
    resources=['#pragma code_page(65001)','#include <windows.h>',
        '1 VERSIONINFO', 'FILEVERSION '+numeric_version, 'PRODUCTVERSION '+numeric_version,
        'FILEFLAGSMASK VS_FFI_FILEFLAGSMASK', 'FILEFLAGS 0', 'FILEOS VOS_NT_WINDOWS32',
        'FILETYPE VFT_APP', 'FILESUBTYPE 0', 'BEGIN', '  BLOCK "StringFileInfo"',
        '  BEGIN', '    BLOCK "040904b0"', '    BEGIN',
        '      VALUE "FileDescription", "'+NAME+'\\0"',
        '      VALUE "ProductName", "'+NAME+'\\0"',
        '      VALUE "FileVersion", "'+file_version+'\\0"',
        '      VALUE "ProductVersion", "'+VERSION+'\\0"',
        '      VALUE "InternalName", "EVACPCLabel\\0"',
        '      VALUE "OriginalFilename", "'+LAUNCHER+'\\0"',
        '    END', '  END', '  BLOCK "VarFileInfo"', '  BEGIN',
        '    VALUE "Translation", 0x0409, 1200', '  END', 'END']
    for entry in entries:
        header.append('{'+quoted(entry['game'])+','+quoted(entry['kicker'])+','+str(entry['resource'])+
                      ',float('+str(entry['focusY'])+'),'+str(entry.get('fit')=='contain').lower()+'},')
        resources.append(str(entry['resource'])+' RCDATA '+quoted((Path(root)/entry['path']).resolve().as_posix()))
    header+=['}};']
    brand=branding(root)
    if brand:
        header+=['inline constexpr unsigned kBrandLogoResource=201;',
                 'inline constexpr unsigned kAppIconResource=1;']
        sources={entry['name']:Path(root)/entry['path'] for entry in brand}
        resources+=['201 RCDATA '+quoted(sources['logo'].resolve().as_posix()),
                    '1 ICON '+quoted(sources['icon'].resolve().as_posix())]
    header+=['inline constexpr char kProductVersion[]='+quoted(VERSION)+';', '}']
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
