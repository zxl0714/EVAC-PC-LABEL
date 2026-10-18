"""Bind a completed build to unchanged sources, tools and matching PE/PDB IDs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

from project import ROOT, digest, write_json_new


def git(*args):
    return subprocess.check_output(['git','-C',str(ROOT),*args])


def dependency_ids(lines):
    result=[]
    for line in lines:
        match=re.match(r'^ ([0-9a-f]{40}) (\S+)(?: \(.*\))?$',line)
        if not match:raise ValueError('Unpinned or absent SDK dependency')
        result.append(dict(path=match.group(2),commit=match.group(1)))
    return result


def snapshot(kind):
    names=set(item.decode('utf-8') for item in git('ls-files','-z','--cached','--others','--exclude-standard').split(b'\0') if item)
    if kind=='sdk':
        explicit={'.gitmodules','scripts/Build-Sdk.ps1','scripts/Build-Common.ps1','scripts/build_receipt.py',
                  'scripts/sdk_sources.py','scripts/project.py',
                  'tools/packages.lock.json','tools/sdk-source.lock.json'}
        names={name for name in names if name.startswith('third_party/rexglue/') or name in explicit}
    files={name:digest(ROOT/name) for name in sorted(names) if (ROOT/name).is_file()}
    modules=dependency_ids(git('submodule','status','--recursive').decode('utf-8').splitlines())
    roots={item['path'] for item in modules}
    for raw in git('status','--porcelain','-z','--ignore-submodules=none','--untracked-files=normal').split(b'\0'):
        if raw and raw[3:].decode('utf-8') in roots:
            raise ValueError('Commit dependency changes and update its pin before building')
    tools={name:digest(ROOT/'tools/apps'/relative) for name,relative in {
        'clang':'llvm/bin/clang.exe','clang++':'llvm/bin/clang++.exe','cmake':'cmake/bin/cmake.exe',
        'ninja':'ninja/ninja.exe','readobj':'llvm/bin/llvm-readobj.exe','pdbutil':'llvm/bin/llvm-pdbutil.exe'}.items()}
    state=dict(files=files,submodules=modules,tools=tools,
        environment={key:os.environ.get(key,'') for key in ('VCToolsVersion','WindowsSDKVersion','VSCMD_ARG_TGT_ARCH')})
    # The SDK may be verified from an ordinary shell without vcvars. Keep the
    # compiler/Windows SDK environment as build metadata, not source identity.
    identity={key:value for key,value in state.items() if key!='environment'}
    return dict(sha256=hashlib.sha256(json.dumps(identity,sort_keys=True).encode()).hexdigest(),**state)


def signature(text,pdb=False):
    guid=re.search(r'\b'+('GUID' if pdb else 'PDBGUID')+r':\s*\{([0-9A-Fa-f-]{36})\}',text)
    age=re.search(r'\b'+('Age' if pdb else 'PDBAge')+r':\s*(\d+)',text)
    if not guid or not age:
        raise ValueError('Missing CodeView/PDB identity')
    return dict(guid=guid.group(1).lower(),age=int(age.group(1)))


def output_root(kind):
    return ROOT/('.deps/sdk-install/bin' if kind=='sdk' else 'out/espgaluda2/bin')


def verify_sdk():
    receipt=json.loads((output_root('sdk')/'build-receipt.json').read_text(encoding='utf-8'))
    if snapshot('sdk')['sha256']!=receipt['sourceSnapshot']['sha256']:
        raise ValueError('SDK sources/tools changed; run Build-Sdk.ps1 before building the host')
    for item in receipt['outputs']:
        if digest(output_root('sdk')/item['path'])!=item['sha256']:
            raise ValueError('Installed SDK differs from its successful build receipt')
    return receipt


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation',choices=('begin','finish','verify-sdk'))
    parser.add_argument('kind',choices=('sdk','host'),nargs='?')
    parser.add_argument('--state',type=Path)
    args=parser.parse_args()
    if args.operation=='verify-sdk':
        verify_sdk();print('SDK source/tool/binary receipt verified.');return
    if not args.kind or not args.state:
        parser.error('begin/finish require a kind and --state')
    receipt_path=output_root(args.kind)/'build-receipt.json'
    if args.operation=='begin':
        if args.kind=='host':verify_sdk()
        current=snapshot(args.kind)
        write_json_new(args.state,dict(format=1,kind=args.kind,sourceSnapshot=current,
            sourceCommit=git('rev-parse','HEAD').decode().strip()))
        if receipt_path.exists():
            receipt_path.rename(args.state.with_suffix('.previous-receipt.json'))
        return
    begin=json.loads(args.state.read_text(encoding='utf-8'))
    if begin['kind']!=args.kind or begin['sourceSnapshot']['sha256']!=snapshot(args.kind)['sha256']:
        raise ValueError('Build inputs changed while compiling; completed output is not certified')
    if begin['sourceSnapshot']['environment']!=snapshot(args.kind)['environment']:
        raise ValueError('MSVC/Windows SDK environment changed while compiling')
    if begin['sourceCommit']!=git('rev-parse','HEAD').decode().strip():
        raise ValueError('Source commit changed while compiling')
    outputs=[]
    binaries=[*sorted(output_root(args.kind).glob('*.exe')),*sorted(output_root(args.kind).glob('*.dll'))]
    if not binaries:raise ValueError('No build outputs found')
    for binary in binaries:
        pdb=binary.with_suffix('.pdb')
        pe_text=subprocess.check_output([str(ROOT/'tools/apps/llvm/bin/llvm-readobj.exe'),
                                        '--coff-debug-directory',str(binary)],text=True,encoding='utf-8')
        pdb_text=subprocess.check_output([str(ROOT/'tools/apps/llvm/bin/llvm-pdbutil.exe'),
                                         'dump','-summary',str(pdb)],text=True,encoding='utf-8')
        pe_id,pdb_id=signature(pe_text),signature(pdb_text,True)
        if pe_id!=pdb_id:raise ValueError(f'PDB does not match {binary.name}')
        for path in (binary,pdb):
            outputs.append(dict(path=path.name,bytes=path.stat().st_size,sha256=digest(path),codeView=pe_id))
    cache=ROOT/('out/sdk-build/CMakeCache.txt' if args.kind=='sdk' else 'out/espgaluda2/CMakeCache.txt')
    write_json_new(receipt_path,dict(**begin,outputs=outputs,cmakeCacheSha256=digest(cache),
        guestExecution=False,allPdbPairsVerified=True))
    print(f'{args.kind} receipt saved; {len(binaries)} binary/PDB pairs verified.')


if __name__=='__main__':main()
