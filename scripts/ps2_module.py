"""Inventory MWo3 overlays and build an analysis-only ELF32 view of their bytes."""
import argparse
from dataclasses import asdict, dataclass
import hashlib
import json
from pathlib import Path
import struct
from ps2_disc import Iso9660, boot_path, elf_info


@dataclass(frozen=True)
class Module:
    module_id: int
    address: int
    text_size: int
    data_size: int
    bss_size: int
    name: str
    sha256: str


def inspect(data):
    if len(data) < 64 or data[:4] != b'MWo3':
        raise ValueError('Expected a complete MWo3 module header')
    module_id, address, text, initialized, bss = struct.unpack_from('<5I', data, 4)
    payload_size=64+text+initialized
    padded_size=(payload_size+2047)&~2047
    if (not text or any(value & 3 for value in (address, text, initialized, bss))
            or address < 0x100000 or address+64+text+initialized+bss > 0x2000000
            or len(data) not in (payload_size,padded_size) or any(data[payload_size:])):
        raise ValueError('Invalid module sizes or EE memory range')
    raw_name = data[32:64].split(b'\0', 1)[0]
    if not raw_name or any(c < 32 or c > 126 or c in b'/\\:' for c in raw_name):
        raise ValueError('Invalid module name')
    return Module(module_id, address, text, initialized, bss,
                  raw_name.decode('ascii'), hashlib.sha256(data).hexdigest())


def analysis_elf(data, entry, roots=()):
    module = inspect(data)
    payload_size=64+module.text_size+module.data_size
    text_start = module.address+64
    text_end = text_start+module.text_size
    if entry & 3 or not text_start <= entry < text_end:
        raise ValueError('Analysis entry is outside module text')
    roots = sorted(set([entry, *roots]))
    if len(roots)>4096 or any(root & 3 or not text_start <= root < text_end for root in roots):
        raise ValueError('Analysis root is outside module text')
    # The player's ISO and memory image are unchanged. The ELF adds section
    # boundaries for Ghidra/recompiler analysis; its payload is byte-identical.
    names = b'\0.module.header\0.module.text\0.module.data\0.module.bss\0.shstrtab\0.strtab\0.symtab\0'
    payload_offset = 0x1000
    name_offset = payload_offset+len(data)
    root_names={root:f'root_ov{module.module_id:02}_{root:08x}' for root in roots}
    strings = b'\0'+b''.join(root_names[root].encode()+b'\0' for root in roots)
    string_offset = name_offset+len(names)
    symbol_offset = (string_offset+len(strings)+3)&~3
    section_offset = symbol_offset+16*(len(roots)+1)
    result = bytearray(section_offset+8*40)
    ident = b'\x7fELF\x01\x01\x01'+bytes(9)
    struct.pack_into('<16sHHIIIIIHHHHHH', result, 0, ident, 2, 8, 1,
                     entry, 52, section_offset, 0x20924001,
                     52, 32, 1, 40, 8, 5)
    struct.pack_into('<8I', result, 52, 1, payload_offset, module.address,
                     module.address, payload_size, payload_size+module.bss_size, 7, 1)
    result[payload_offset:payload_offset+len(data)] = data
    result[name_offset:name_offset+len(names)] = names
    result[string_offset:string_offset+len(strings)] = strings
    for index, root in enumerate(roots, 1):
        struct.pack_into('<IIIBBH', result, symbol_offset+16*index,
                         strings.index(root_names[root].encode()), root, 0, 0x12, 0, 2)
    sections = [
        ('.module.header', 1, 3, module.address, payload_offset, 64),
        ('.module.text', 1, 7, text_start, payload_offset+64, module.text_size),
        ('.module.data', 1, 3, text_end, payload_offset+64+module.text_size, module.data_size),
        ('.module.bss', 8, 3, module.address+payload_size, payload_offset+payload_size, module.bss_size),
        ('.shstrtab', 3, 0, 0, name_offset, len(names)),
        ('.strtab', 3, 0, 0, string_offset, len(strings)),
        ('.symtab', 2, 0, 0, symbol_offset, 16*(len(roots)+1)),
    ]
    for index, (name, kind, flags, address, offset, size) in enumerate(sections, 1):
        struct.pack_into('<10I', result, section_offset+index*40,
                         names.index(name.encode()), kind, flags, address,
                         offset, size, 0, 0, 4, 0)
    struct.pack_into('<4I', result, section_offset+7*40+24, 6, 1, 4, 16)
    return bytes(result)


def module_roots(boot, module, declaration):
    roots={int(declaration['entry'],0),*(int(root,0) for root in declaration.get('roots',[]))}
    segments=elf_info(boot)['segments']
    for low, high in declaration.get('bootCallRanges',[]):
        low, high=int(low,0),int(high,0)
        if low & 3 or high & 3 or high<=low or high-low>0x10000:
            raise ValueError('Invalid original initializer range')
        for pc in range(low,high,4):
            segment=next((s for s in segments if s['permissions']&1 and
                          int(s['address'],16)<=pc<=int(s['address'],16)+s['fileSize']-4),None)
            if segment is None:
                raise ValueError('Initializer range is outside original boot code')
            offset=segment['offset']+pc-int(segment['address'],16)
            word=struct.unpack_from('<I',boot,offset)[0]
            if word>>26==3:
                target=((pc+4)&0xf0000000)|((word&0x3ffffff)<<2)
                if module.address+64<=target<module.address+64+module.text_size:
                    roots.add(target)
    # Overlay callbacks can live in the boot program's data, rather than in
    # the overlay itself. Read only reviewed tables for the active module.
    for low,high in declaration.get('bootCallbackTables',[]):
        low,high=int(low,0),int(high,0)
        if low&3 or high&3 or high<=low or high-low>0x10000:
            raise ValueError('Invalid boot callback table')
        for address in range(low,high,4):
            segment=next((s for s in segments if int(s['address'],16)<=address<=
                          int(s['address'],16)+s['fileSize']-4),None)
            if segment is None:
                raise ValueError('Callback table is outside initialized boot data')
            offset=segment['offset']+address-int(segment['address'],16)
            target=struct.unpack_from('<I',boot,offset)[0]
            if module.address+64<=target<module.address+64+module.text_size:
                roots.add(target)
    if any(root & 3 or not module.address+64<=root<module.address+64+module.text_size for root in roots):
        raise ValueError('Declared initializer is outside module text')
    return sorted(roots)


def prepare_modules(iso, manifest, output):
    iso, manifest, output=Path(iso),Path(manifest),Path(output)
    spec=json.loads(manifest.read_text(encoding='utf-8'))
    if spec.get('format')!=1 or not isinstance(spec.get('modules'),list) or not spec['modules']:
        raise ValueError('Invalid module manifest')
    if output.exists():
        raise FileExistsError('Use a fresh module preparation directory')
    prepared=[]
    with iso.open('rb') as stream:
        disc=Iso9660(stream,iso.stat().st_size);entries=disc.entries()
        boot_name=boot_path(disc.contents(entries['SYSTEM.CNF'],65536))
        boot=disc.contents(entries[boot_name])
        if hashlib.sha256(boot).hexdigest()!=spec.get('bootSha256'):
            raise ValueError('Boot program does not match module manifest')
        identities=set()
        for declaration in spec['modules']:
            data=disc.contents(entries[declaration['discPath']]);module=inspect(data)
            identity=(module.address,module.module_id)
            if identity in identities:
                raise ValueError('Duplicate module identity')
            identities.add(identity)
            expected=(declaration['id'],int(declaration['address'],0),
                      int(declaration['textSize'],0),int(declaration['dataSize'],0),
                      int(declaration['bssSize'],0),declaration['fileSize'],declaration['sha256'])
            actual=(module.module_id,module.address,module.text_size,module.data_size,
                    module.bss_size,len(data),module.sha256)
            if actual!=expected:
                raise ValueError('Original module differs from manifest: '+declaration['discPath'])
            roots=module_roots(boot,module,declaration)
            image=analysis_elf(data,int(declaration['entry'],0),roots)
            prepared.append((module,image,roots))
    output.mkdir(parents=True)
    (output/'boot.elf').write_bytes(boot)
    receipt={'format':1,'bootSha256':spec['bootSha256'],
             'manifestSha256':hashlib.sha256(manifest.read_bytes()).hexdigest(),'modules':[]}
    for module,image,roots in prepared:
        directory=output/f'module-{module.module_id:02}';directory.mkdir()
        (directory/'analysis.elf').write_bytes(image)
        (directory/'roots.txt').write_text(''.join(f'{root:08x}\n' for root in roots),encoding='ascii')
        receipt['modules'].append(dict(asdict(module),analysisSha256=hashlib.sha256(image).hexdigest(),roots=roots))
    (output/'prepared.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8',newline='\n')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('module', type=Path, nargs='?')
    parser.add_argument('--analysis-elf', type=Path)
    parser.add_argument('--entry', type=lambda value: int(value, 0))
    parser.add_argument('--roots', type=Path, help='Verified original call targets, one hex address per line')
    parser.add_argument('--iso', type=Path)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.iso or args.manifest or args.output:
        if not all((args.iso,args.manifest,args.output)) or args.module or args.analysis_elf:
            parser.error('--iso, --manifest and --output are required together')
        receipt=prepare_modules(args.iso,args.manifest,args.output)
        print(json.dumps({'modules':len(receipt['modules']),'output':str(args.output)},indent=2))
        raise SystemExit(0)
    if args.module is None:
        parser.error('a module file is required')
    data = args.module.read_bytes()
    module = inspect(data)
    if args.analysis_elf:
        if args.entry is None:
            parser.error('--entry is required with --analysis-elf')
        args.analysis_elf.parent.mkdir(parents=True, exist_ok=True)
        roots = [int(line, 16) for line in args.roots.read_text().splitlines() if line.strip()] if args.roots else []
        with args.analysis_elf.open('xb') as output:
            output.write(analysis_elf(data, args.entry, roots))
    print(json.dumps(asdict(module), indent=2))
