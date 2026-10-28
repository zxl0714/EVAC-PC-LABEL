"""Relink isolated shad observer objects; never rebuild the baseline in place."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from project import digest,write_json_new
from toolchain import tool_path
from rebuild_ps4_probe import split_windows,normalized


def shorten_link(step,destination):
    args=step['command'];position=args.index('--')+2
    response=destination/'private-link-direct.rsp'
    with response.open('x',encoding='utf-8') as stream:
        # vs_link_exe expands @files back into a Windows command line. Invoke
        # the original linker directly, retaining its printed final manifest
        # option, so the response file remains a real linker response file.
        stream.write(subprocess.list2cmdline([*args[position:],'/MANIFEST:EMBED,ID=1']))
    return dict(step,command=[args[position-1],'@'+str(response)],
                response_file=dict(path=str(response),sha256=digest(response)))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture',type=Path,required=True)
    parser.add_argument('--destination',type=Path,required=True)
    parser.add_argument('--execute',action='store_true')
    parser.add_argument('--resume-link',action='store_true')
    args=parser.parse_args()
    base=ROOT/'.local/input-latency-v080-r1'
    fixture,destination=args.fixture.resolve(strict=True),args.destination.resolve()
    if not all(p.is_relative_to(base) for p in (fixture,destination)) or (destination.exists() and not args.resume_link):
        raise ValueError('Verified private fixture/fresh output required')
    preparation=json.loads((fixture/'preparation.json').read_text())
    for item in preparation['files']:
        if digest(Path(item['source']))!=item['original_sha256'] or digest(Path(item['copy']))!=item['copy_sha256']:
            raise ValueError('Private preparation or original baseline changed')
    if args.resume_link:
        prior=json.loads((destination/'build-plan.json').read_text())
        if prior['preparation_sha256']!=digest(fixture/'preparation.json') or (destination/'shadps4.exe').exists():
            raise ValueError('Not the same unfinished private link')
        for name,identity in prior['reused_input_sha256'].items():
            if digest(Path(name))!=identity: raise ValueError('Original build input changed')
        objects=[dict(path=s['output'],sha256=digest(Path(s['output']))) for s in prior['commands'] if s['stage']=='compile']
        step=shorten_link(prior['commands'][-1],destination)
        write_json_new(destination/'resume-link-direct-plan.json',dict(command=step,compiled_objects=objects,
            original_plan_sha256=digest(destination/'build-plan.json')))
        if not args.execute: print('Prepared resume link:',destination);return
        subprocess.run(step['command'],cwd=step['cwd'],check=True)
        for item in objects:
            if digest(Path(item['path']))!=item['sha256']: raise ValueError('Compiled observer object changed')
        for name,identity in prior['reused_input_sha256'].items():
            if digest(Path(name))!=identity: raise ValueError('Original link input changed')
        write_json_new(destination/'build-result.json',dict(files=[*objects,
            dict(path=step['output'],sha256=digest(Path(step['output'])))],original_inputs_unchanged=True,
            plan_sha256=digest(destination/'build-plan.json'),resume_sha256=digest(destination/'resume-link-direct-plan.json'),release_certified=False))
        print('Private shad measurement link complete:',destination);return
    original=base/'shadps4-build';ninja=tool_path('ninja',ROOT)
    entries=json.loads(subprocess.check_output([str(ninja),'-C',str(original),'-t','compdb',
        'CXX_COMPILER__shadps4_unscanned_Release'],encoding='utf-8'))
    destination.mkdir();plan=[];replacements={};identities={}
    for name in ('linker.cpp','vk_presenter.cpp','vk_swapchain.cpp'):
        entry,=[e for e in entries if Path(e['file']).name==name]
        path=Path(entry['file']);output=destination/(name+'.obj')
        replacements[normalized(entry['output'])]=str(output)
        rewritten=[];source_changed=output_changed=False
        for arg in split_windows(entry['command']):
            if arg.startswith('/Fo'): rewritten.append('/Fo'+str(output));output_changed=True
            elif arg.startswith('/Fd') or arg.lower()=='/showincludes': continue
            elif arg.lower() in ('/zi','-zi'): rewritten.append('/Z7')
            elif normalized(arg)==normalized(path): rewritten.append(str(fixture/name));source_changed=True
            else: rewritten.append(arg)
        if not source_changed or not output_changed: raise ValueError('Unredirected compiler outputs')
        position=rewritten.index('--')
        rewritten[position:position]=['-I'+str(path.parent),'-I'+str(fixture),'-I'+str(ROOT/'tools/latency')]
        plan.append(dict(stage='compile',cwd=str(original),command=rewritten,output=str(output)))
    links=json.loads(subprocess.check_output([str(ninja),'-C',str(original),'-t','compdb','-x',
        'CXX_EXECUTABLE_LINKER__shadps4_Release'],encoding='utf-8'))
    link,=[e for e in links if e['output']=='shadps4.exe']
    match=re.fullmatch(r'.*? /C "cd \. && (.+) && cd \."',link['command'])
    if not match: raise ValueError('Unexpected original linker wrapper')
    rewrite=[];changed=set();outputs=set()
    for arg in split_windows(match[1]):
        lower=arg.lower();key=normalized(arg)
        if arg.startswith('@'): raise ValueError('Unexpanded linker response file')
        if lower.startswith('--intdir='):
            folder=destination/'link';folder.mkdir();rewrite.append('--intdir='+str(folder))
        elif key in replacements: rewrite.append(replacements[key]);changed.add(key)
        elif any(lower.startswith(p) for p in ('/out:','/pdb:','/implib:')):
            option,value=arg.split(':',1);outputs.add(option.lower())
            rewrite.append(option+':'+str(destination/Path(value).name))
        elif lower=='/incremental': rewrite.append('/INCREMENTAL:NO')
        else: rewrite.append(arg)
        candidate=Path(arg) if Path(arg).is_absolute() else original/arg
        if lower.endswith(('.obj','.lib','.res')) and candidate.is_file():
            identities[str(candidate.resolve())]=digest(candidate)
    if changed!=replacements.keys() or '/out' not in outputs: raise ValueError('Incomplete link redirection')
    plan.append(shorten_link(dict(stage='link',cwd=str(original),command=rewrite,output=str(destination/'shadps4.exe')),destination))
    write_json_new(destination/'build-plan.json',dict(commands=plan,reused_input_sha256=identities,
        preparation_sha256=digest(fixture/'preparation.json')))
    if not args.execute: print('Prepared only:',destination);return
    for i,step in enumerate(plan,1):
        print(f"[{i}/{len(plan)}] {step['stage']}: {Path(step['output']).name}",flush=True)
        subprocess.run(step['command'],cwd=step['cwd'],check=True)
    for path,identity in identities.items():
        if digest(Path(path))!=identity: raise ValueError('Original build object/library changed')
    for item in preparation['files']:
        if digest(Path(item['source']))!=item['original_sha256']: raise ValueError('Original source changed')
    write_json_new(destination/'build-result.json',dict(files=[dict(path=s['output'],sha256=digest(Path(s['output']))) for s in plan],
        original_inputs_unchanged=True,plan_sha256=digest(destination/'build-plan.json'),release_certified=False))
    print('Private shad measurement build complete:',destination)


if __name__=='__main__': main()
