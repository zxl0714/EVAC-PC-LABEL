"""Read a complete CUSA09062 app/1.02 patch inventory; output metadata only."""
import argparse
from pathlib import Path
import struct

from analysis_paths import private_output
from project import digest,write_json_new
from xex_inventory import files


def sfo(path):
    if Path(path).stat().st_size>1024*1024:raise ValueError('Invalid SFO size')
    return parse_sfo(Path(path).read_bytes())


def parse_sfo(raw):
    """Parse bounded SFO bytes, including metadata read directly from an archive."""
    if len(raw)<20 or len(raw)>1024*1024:raise ValueError('Invalid SFO size')
    magic,version,keys,data,count=struct.unpack_from('<5I',raw)
    if magic!=0x46535000 or version!=0x101 or count>4096 or 20+count*16>keys or not keys<=data<=len(raw):
        raise ValueError('Invalid SFO header')
    values={}
    for index in range(count):
        name,kind,length,capacity,offset=struct.unpack_from('<HHIII',raw,20+index*16)
        if keys+name>=data or length>capacity or offset>len(raw)-data or capacity>len(raw)-data-offset:
            raise ValueError('SFO field extent is out of bounds')
        end=raw.find(b'\0',keys+name,data)
        if end<0:raise ValueError('Unterminated SFO key')
        key=raw[keys+name:end].decode('ascii')
        if key in values:raise ValueError('Duplicate SFO key')
        value=raw[data+offset:data+offset+length]
        if kind==0x204:
            if not value or value[-1:]!=b'\0' or b'\0' in value[:-1]:raise ValueError('Invalid SFO text')
            values[key]=value[:-1].decode('utf-8')
        elif kind==0x404:
            if length!=4:raise ValueError('Invalid SFO integer')
            values[key]=struct.unpack('<I',value)[0]
        else:values[key]=dict(format=kind,bytes=length)
    return values


def inventory(root):
    root=Path(root).resolve()
    records=[]
    for path in sorted(files(root)):
        size=path.stat().st_size; name=path.relative_to(root).as_posix()
        if name=='CUSA09062.complete' and size==0:continue # Optional extraction marker, not a game resource.
        item=dict(path=name,bytes=size,sha256=digest(path),program=path.name=='eboot.bin' or path.suffix.lower()=='.prx')
        if size!=path.stat().st_size:raise ValueError('Game input changed during inventory')
        records.append(item)
    names={item['path'].casefold() for item in records}
    if len(names)!=len(records):raise ValueError('Case-colliding game paths')
    if any(not name.startswith(('cusa09062-app/','cusa09062-patch/')) for name in names):
        raise ValueError('Expected only CUSA09062-app and CUSA09062-patch under the data root')
    for prefix,version,category in [('CUSA09062-app','01.00','gd'),('CUSA09062-patch','01.02','gp')]:
        metadata=sfo(root/prefix/'sce_sys/param.sfo')
        if metadata.get('TITLE_ID')!='CUSA09062' or metadata.get('APP_VER')!=version or metadata.get('CATEGORY')!=category:
            raise ValueError('Unsupported Ketsui title, app version or patch version')
        if (prefix+'/eboot.bin').casefold() not in names:raise ValueError('Missing app/patch executable')
    return dict(format=1,game='ketsui',titleId='CUSA09062',version='01.02',source=dict(
        repository='https://github.com/zxl0714/Ketsui-PC',commit='9501378a609d39f20e1d72d41664986ceb167f99',
        description='Complete supported app and 1.02 patch; paths, sizes and SHA-256 metadata only'),files=records)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-data',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    output=private_output(args.output,args.game_data)
    record=inventory(args.game_data)
    write_json_new(output,record)
    print(f'{len(record["files"])} read-only files, {sum(item["bytes"] for item in record["files"])} bytes; metadata: {output}')


if __name__=='__main__':main()
