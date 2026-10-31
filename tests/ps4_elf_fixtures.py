"""Authored ELF64 data, never extracted game instructions or resources."""
import struct


def module(*, tls=False, duplicate=False, arrays=False, init=True, untyped=None, referenced=False):
    raw=bytearray(0x800)
    struct.pack_into('<16sHHIQQQIHHHHHH',raw,0,b'\x7fELF\x02\x01\x01'+bytes(9),0xfe18,62,1,
                     0,64,0,0,64,56,4 if tls else 3,0,0,0)
    struct.pack_into('<II6Q',raw,64,1,5,0x200,0,0,64,4096,4096)
    strings=b'\0libFixture\0AuthoredNid#A#A\0'
    optional=len(strings)
    if untyped is not None: strings+=b'OptionalFixtureSymbol\0'
    tags=[(0x61000035,0),(0x61000037,len(strings)),(0x61000039,0x80),
          (0x6100003f,72 if duplicate or untyped is not None else 48),(12 if init else 0x7000,0),(13,16),
          (0x61000013,1),(0x6100000d,1)]
    if arrays: tags.append((27,8))
    if referenced: tags.extend(((0x6100002f,0xe0),(0x61000031,24)))
    tags.append((0,0))
    struct.pack_into('<II6Q',raw,120,2,6,0x300,0,0,len(tags)*16,len(tags)*16,8)
    struct.pack_into('<II6Q',raw,176,0x61000000,4,0x500,0,0,0x100,0,16)
    if tls: struct.pack_into('<II6Q',raw,232,7,4,0x240,0,0,1,16,16)
    raw[0x200:0x203]=bytes.fromhex('31 c0 c3')
    for index,item in enumerate(tags): struct.pack_into('<qQ',raw,0x300+index*16,*item)
    raw[0x500:0x500+len(strings)]=strings
    for index in range(1,3 if duplicate else 2):
        struct.pack_into('<IBBHQQ',raw,0x580+24*index,12,0x12,0,1,32,1)
    if untyped is not None: struct.pack_into('<IBBHQQ',raw,0x5b0,optional,untyped<<4,0,0,0,0)
    if referenced: struct.pack_into('<QQq',raw,0x5e0,48,(2<<32)|6,0)
    return raw


def executable():
    raw=module()
    struct.pack_into('<H',raw,16,0xfe10)
    struct.pack_into('<Q',raw,24,2) # Authored RET in the file-backed text segment.
    return raw


def unwind_executable(duplicate=False):
    raw=executable()
    struct.pack_into('<H',raw,56,5 if duplicate else 4)
    struct.pack_into('<II6Q',raw,232,0x6474e550,4,0x230,0x30,0,16,16,4)
    if duplicate:
        struct.pack_into('<II6Q',raw,288,0x6474e550,4,0x230,0x30,0,16,16,4)
    return raw


def libc():
    raw=module()
    name=b'\0libFixture\0bzQExy189ZI#A#A\0'
    raw[0x500:0x500+len(name)]=name
    struct.pack_into('<Q',raw,0x300+16+8,len(name))
    raw[0x220]=0xc3
    return raw


def plain_self(elf,version_note=False):
    """Authored plain SELF segment wrapper, not a signed or encrypted file."""
    elf=bytearray(elf)
    header=struct.unpack_from('<16sHHIQQQIHHHHHH',elf)
    if version_note:
        struct.pack_into('<II6Q',elf,header[5]+56*header[10],0x6fffff01,4,0x700,0,0,3,3,1)
        struct.pack_into('<H',elf,56,header[10]+1)
        header=struct.unpack_from('<16sHHIQQQIHHHHHH',elf)
    all_programs=[struct.unpack_from('<II6Q',elf,header[5]+56*i) for i in range(header[10])]
    programs=[p for p in all_programs if p[0]!=0x6fffff01]
    embedded=32+32*len(programs)
    table_end=header[5]+56*header[10]
    header_bytes=(embedded+table_end+15)&~15
    raw=bytearray(header_bytes)
    raw[:10]=bytes.fromhex('4f153d1d000101120101')
    struct.pack_into('<HH',raw,12,header_bytes,0)
    struct.pack_into('<HH',raw,24,len(programs),0x22)
    raw[embedded:embedded+table_end]=elf[:table_end]
    for index,p in enumerate(programs):
        struct.pack_into('<4Q',raw,32+index*32,(index<<20)|0x800,len(raw),p[5],p[5])
        raw+=elf[p[2]:p[2]+p[5]]
    struct.pack_into('<Q',raw,16,len(raw))
    if version_note:raw+=b'\x02v1'
    return raw


def cases():
    result=[('executable',executable(),False,None,True),('module',module(),True,None,True),
            ('unwind-header',unwind_executable(),False,None,True),
            ('duplicate-unwind-header',unwind_executable(True),False,None,False),
            ('verified-libc',executable(),False,libc(),True),
            ('weak-unreferenced',module(untyped=2),True,None,True),
            ('tls',module(tls=True),True,None,False),('duplicate-export',module(duplicate=True),True,None,False),
            ('init-array',module(arrays=True),True,None,False),('missing-init',module(init=False),True,None,False),
            ('weak-referenced',module(untyped=2,referenced=True),True,None,False),
            ('strong-untyped',module(untyped=1),True,None,False),
            ('wrong-mode',executable(),True,None,False)]
    import_relocation=module(untyped=1,referenced=True)
    import_relocation[0x5b4]=0x12
    result.append(('import-relocation',import_relocation,True,None,True))
    relative=bytearray(import_relocation)
    struct.pack_into('<QQq',relative,0x5e0,48,8,12)
    result.append(('relative-relocation',relative,True,None,True))
    defined=bytearray(import_relocation)
    struct.pack_into('<QQq',defined,0x5e0,48,(1<<32)|1,-7)
    result.append(('defined-relocation',defined,True,None,True))
    absolute=bytearray(defined)
    struct.pack_into('<H',absolute,16,0xfe10)
    struct.pack_into('<Q',absolute,24,2)
    struct.pack_into('<H',absolute,0x598+6,0xfff1)
    result.append(('absolute-relocation',absolute,False,None,True))
    # Scalar corruption remains bounded and must fail before publishing output.
    changes=[('machine','<H',18,183),('entry-zero-fill','<Q',24,128),
             ('file-extent','<Q',64+8,0x10000),('segment-flags','<I',64+4,8),
             ('segment-alignment','<Q',64+48,3),('header-count','<H',56,65),
             ('symbol-string','<I',0x580,1000),('symbol-size','<Q',0x300+3*16+8,25),
             ('dynamic-extent','<Q',0x300+2*16+8,0x1000)]
    for name,layout,offset,value in changes:
        raw=executable(); struct.pack_into(layout,raw,offset,value)
        result.append((name,raw,False,None,False))
    bad_contract=libc();bad_contract[0x220]=0x90
    result.append(('plain-self-libc',executable(),False,plain_self(libc()),True))
    result.append(('plain-self-note-libc',executable(),False,plain_self(libc(),True),True))
    result.append(('plain-self-executable',plain_self(executable()),False,plain_self(libc()),True))
    result.append(('plain-self-module',plain_self(module()),True,plain_self(libc()),True))
    bad_source=plain_self(module());struct.pack_into('<Q',bad_source,32,0x802)
    result.append(('encrypted-source-module',bad_source,True,None,False))
    for name,offset,value in [('encrypted',32,0x802),('compressed',32,0x808),
                              ('duplicate',64,0x800),('bad-program',32,0xfff00800)]:
        bad=plain_self(libc());struct.pack_into('<Q',bad,offset,value)
        result.append(('self-'+name,executable(),False,bad,False))
    bad=plain_self(libc());bad.pop()
    result.append(('self-truncated',executable(),False,bad,False))
    result.extend((('bad-libc-ret',executable(),False,bad_contract,False),
                   ('empty-libc',executable(),False,b'',False),
                   ('truncated',executable()[:20],False,None,False)))
    return result
