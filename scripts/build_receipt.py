"""Bind a completed build to unchanged sources, tools and matching PE/PDB IDs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

from project import ROOT, digest, reject_link, write_json_new
from generate_catalogue import relative, load
from toolchain import tool_path, tool_snapshot

DEFAULT_GAMES=('espgaluda2',)


def selected_games(value):
    games=tuple(sorted(value.split(',')))
    if not games or len(set(games))!=len(games) or any(game not in ('espgaluda2','futari','sdoj') for game in games):
        raise ValueError('Select unique supported Xbox 360 games')
    return games


def git(*args):
    return subprocess.check_output(['git','-C',str(ROOT),*args])


def dependency_ids(lines):
    result=[]
    for line in lines:
        match=re.match(r'^ ([0-9a-f]{40}) (\S+)(?: \(.*\))?$',line)
        if not match:raise ValueError('Unpinned or absent SDK dependency')
        result.append(dict(path=match.group(2),commit=match.group(1)))
    return result


def snapshot(kind,games=DEFAULT_GAMES):
    names=set(item.decode('utf-8') for item in git('ls-files','-z','--cached','--others','--exclude-standard').split(b'\0') if item)
    if kind=='sdk':
        explicit={'.gitmodules','scripts/Build-Sdk.ps1','scripts/Build-Common.ps1','scripts/build_receipt.py',
                  'scripts/sdk_sources.py','scripts/project.py','scripts/toolchain.py',
                  'scripts/Prepare-Tools.ps1','scripts/prepare_tools.py','tools/python-tools.lock.txt',
                  'tools/packages.lock.json','tools/sdk-source.lock.json'}
        names={name for name in names if name.startswith('third_party/rexglue/') or name in explicit}
    files={name:digest(ROOT/name) for name in sorted(names) if (ROOT/name).is_file()}
    modules=dependency_ids(git('submodule','status','--recursive').decode('utf-8').splitlines())
    roots={item['path'] for item in modules}
    for raw in git('status','--porcelain','-z','--ignore-submodules=none','--untracked-files=normal').split(b'\0'):
        if raw and raw[3:].decode('utf-8') in roots:
            raise ValueError('Commit dependency changes and update its pin before building')
    tools=tool_snapshot(ROOT)
    state=dict(files=files,submodules=modules,tools=tools,
        environment={key:os.environ.get(key,'') for key in ('VCToolsVersion','WindowsSDKVersion','VSCMD_ARG_TGT_ARCH')})
    if kind=='host':
        catalogue={game['id']:game for game in load(ROOT/'games/catalogue.json')}
        state['generated']={}
        for game in games:
            stamp=ROOT/'generated'/game/'generation.json'
            reject_link(stamp)
            generation=json.loads(stamp.read_text(encoding='utf-8'))
            expected_modules={'default' if Path(item['path']).name=='default.xex' else Path(item['path']).stem
                              for item in catalogue[game]['files'] if item['program']}
            if (generation.get('game')!=game or generation.get('guestExecution') is not False or
                    generation.get('gameDataUnchanged') is not True or
                    len(generation.get('modules',[]))!=len(expected_modules) or
                    set(generation.get('modules',[]))!=expected_modules):
                raise ValueError('Incomplete generation receipt: '+game)
            generated={}
            for item in generation['outputs']:
                name=relative(item['path'])
                if not name.startswith('generated/'+game+'/') or name in generated:
                    raise ValueError('Invalid generated source path')
                path=ROOT/name
                reject_link(path)
                if digest(path)!=item['sha256']:
                    raise ValueError('Generated sources changed; regenerate before building')
                generated[name]=item['sha256']
            if not generated:
                raise ValueError('No generated sources')
            for name,expected in generation['inputs'].items():
                relative(name)
                if digest(ROOT/name)!=expected:
                    raise ValueError('Generation inputs changed; regenerate before building')
            state['generated'][game]=dict(receiptSha256=digest(stamp),files=generated)
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


def output_root(kind,games=DEFAULT_GAMES):
    name=games[0] if len(games)==1 else 'xbox360'
    return ROOT/('.deps/sdk-install/bin' if kind=='sdk' else 'out/'+name+'/bin')


def verify_receipt(kind,games=DEFAULT_GAMES):
    receipt=json.loads((output_root(kind,games)/'build-receipt.json').read_text(encoding='utf-8'))
    if kind=='host' and receipt.get('games')!=list(games):
        raise ValueError('Host receipt belongs to a different game selection')
    if snapshot(kind,games)['sha256']!=receipt['sourceSnapshot']['sha256']:
        raise ValueError(f'{kind} sources/tools changed; rebuild before using this output')
    for item in receipt['outputs']:
        name=relative(item['path'])
        if '/' in name or digest(output_root(kind,games)/name)!=item['sha256']:
            raise ValueError('Installed output differs from its successful build receipt')
    return receipt


def verify_sdk():
    return verify_receipt('sdk')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation',choices=('begin','finish','verify-sdk','verify-host'))
    parser.add_argument('kind',choices=('sdk','host'),nargs='?')
    parser.add_argument('--state',type=Path)
    parser.add_argument('--games',type=selected_games,default=DEFAULT_GAMES)
    args=parser.parse_args()
    if args.operation=='verify-sdk':
        verify_sdk();print('SDK source/tool/binary receipt verified.');return
    if args.operation=='verify-host':
        verify_sdk();verify_receipt('host',args.games);print('Host source/generated/tool/binary receipt verified.');return
    if not args.kind or not args.state:
        parser.error('begin/finish require a kind and --state')
    receipt_path=output_root(args.kind,args.games)/'build-receipt.json'
    if args.operation=='begin':
        if args.kind=='host':verify_sdk()
        current=snapshot(args.kind,args.games)
        write_json_new(args.state,dict(format=1,kind=args.kind,games=list(args.games) if args.kind=='host' else [],sourceSnapshot=current,
            sourceCommit=git('rev-parse','HEAD').decode().strip()))
        if receipt_path.exists():
            receipt_path.rename(args.state.with_suffix('.previous-receipt.json'))
        return
    begin=json.loads(args.state.read_text(encoding='utf-8'))
    if begin['kind']!=args.kind or begin['sourceSnapshot']['sha256']!=snapshot(args.kind,args.games)['sha256']:
        raise ValueError('Build inputs changed while compiling; completed output is not certified')
    if begin['sourceSnapshot']['environment']!=snapshot(args.kind,args.games)['environment']:
        raise ValueError('MSVC/Windows SDK environment changed while compiling')
    if begin['sourceCommit']!=git('rev-parse','HEAD').decode().strip():
        raise ValueError('Source commit changed while compiling')
    outputs=[]
    binaries=[*sorted(output_root(args.kind,args.games).glob('*.exe')),*sorted(output_root(args.kind,args.games).glob('*.dll'))]
    if not binaries:raise ValueError('No build outputs found')
    readobj,pdbutil=tool_path('readobj',ROOT),tool_path('pdbutil',ROOT)
    for binary in binaries:
        pdb=binary.with_suffix('.pdb')
        pe_text=subprocess.check_output([str(readobj),
                                        '--coff-debug-directory',str(binary)],text=True,encoding='utf-8')
        pdb_text=subprocess.check_output([str(pdbutil),
                                         'dump','-summary',str(pdb)],text=True,encoding='utf-8')
        pe_id,pdb_id=signature(pe_text),signature(pdb_text,True)
        if pe_id!=pdb_id:raise ValueError(f'PDB does not match {binary.name}')
        for path in (binary,pdb):
            outputs.append(dict(path=path.name,bytes=path.stat().st_size,sha256=digest(path),codeView=pe_id))
    cache=ROOT/'out/sdk-build/CMakeCache.txt' if args.kind=='sdk' else output_root(args.kind,args.games).parent/'CMakeCache.txt'
    write_json_new(receipt_path,dict(**begin,outputs=outputs,cmakeCacheSha256=digest(cache),
        guestExecution=False,allPdbPairsVerified=True))
    print(f'{args.kind} receipt saved; {len(binaries)} binary/PDB pairs verified.')


if __name__=='__main__':main()
