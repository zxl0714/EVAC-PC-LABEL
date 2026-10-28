"""Relink a private SDOJ host/input probe; retain all original game modules."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from project import digest, write_json_new
from toolchain import tool_path
from prepare_futari_probe import replace_once
from rebuild_g2_probe import compile_command
from rebuild_ps4_probe import normalized, split_windows


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination',type=Path,required=True)
    args=parser.parse_args()
    destination=args.destination.resolve()
    private=ROOT/'.local/input-latency-v080-r1'
    if not destination.is_relative_to(private) or destination.exists():
        raise ValueError('Fresh private output required')
    host_path=ROOT/'games/sdoj/host.cpp'
    input_path=ROOT/'backends/xbox360/input/configured_input.cpp'
    host='#include "futari_probe_exports.h"\n'+host_path.read_text(encoding='utf-8')
    host=replace_once(host,'void OnGameSetup() override {',
        'void OnGameSetup() override {\n        FutariProbeInitialize();')
    host=replace_once(host,'void OnGameStop() override {',
        'void OnGameStop() override {\n        FutariProbeFlush();')
    text='#include "futari_probe_exports.h"\n'+input_path.read_text(encoding='utf-8')
    text=replace_once(text,'X_RESULT GetState(uint32_t user, X_INPUT_STATE* output) override {',
        'X_RESULT GetState(uint32_t user, X_INPUT_STATE* output) override {\n'
        '        if (user == 0 && output && FutariProbeInput(output)) return X_ERROR_SUCCESS;')
    destination.mkdir()
    (destination/'host.cpp').write_text(host,encoding='utf-8')
    (destination/'configured_input.cpp').write_text(text,encoding='utf-8')
    build=ROOT/'out/xbox360'
    ninja=str(tool_path('ninja'))
    def database(rule):
        return json.loads(subprocess.check_output([ninja,'-C',str(build),'-t','compdb','-x',rule],encoding='utf-8'))
    entries=database('CXX_COMPILER__sdoj_unscanned_Release')
    host_entry,=[e for e in entries if normalized(e['file'])==normalized(host_path)]
    input_entries=database('CXX_COMPILER__cave_xbox360_input_unscanned_Release')
    if {Path(e['file']).name for e in input_entries}!={'configured_input.cpp','input_state.cpp'}:
        raise ValueError('Configured input library composition changed')
    input_entry=next(e for e in input_entries if Path(e['file']).name=='configured_input.cpp')
    input_state=next(e['output'] for e in input_entries if Path(e['file']).name=='input_state.cpp')
    inc=[ROOT/'tools/latency',ROOT/'games/sdoj',ROOT/'backends/xbox360/input']
    jobs=[]
    for entry,source,name in ((host_entry,destination/'host.cpp','host'),
            (host_entry,ROOT/'tools/latency/futari_probe_host.cpp','probe-host'),
            (input_entry,destination/'configured_input.cpp','configured_input')):
        output=destination/(name+'.obj')
        jobs.append(dict(source=str(source),output=str(output),
                         command=compile_command(entry,source,output,inc,True)))
    entry,=[e for e in database('CXX_EXECUTABLE_LINKER__sdoj_Release') if Path(e['output']).name=='sdoj.exe']
    pieces=entry['command'].split(' && ')
    if len(pieces)<3 or not pieces[0].endswith('/C "cd .'):
        raise ValueError('Unexpected linker wrapper')
    replacements={normalized(host_entry['output']):[jobs[0]['output']],
        normalized('backends/xbox360/cave_xbox360_input.lib'):[jobs[2]['output'],input_state]}
    command,seen,kinds=[],set(),set()
    source_inputs={str(p):digest(p) for p in (host_path,input_path,Path(__file__),
        ROOT/'tools/latency/futari_probe_host.cpp',ROOT/'tools/latency/futari_probe_exports.h',
        ROOT/'tools/latency/latency_probe.h',ROOT/'tools/latency/test_pad.h')}
    args_iter=iter(split_windows(pieces[1]))
    for arg in args_iter:
        key=normalized(arg)
        if arg=='-o':
            next(args_iter);command.extend(['-o',str(destination/'sdoj.exe')]);kinds.add('exe')
        elif key.startswith('/implib:'):
            command.append('/implib:'+str(destination/'sdoj.lib'));kinds.add('lib')
        elif key.startswith('/pdb:'):
            command.append('/pdb:'+str(destination/'sdoj.pdb'));kinds.add('pdb')
        elif key in replacements:
            command.extend(replacements[key]);seen.add(key)
        else:
            command.append(arg)
        path=Path(arg) if Path(arg).is_absolute() else build/arg
        if key.endswith(('.obj','.lib')) and path.is_file():
            source_inputs[str(path.resolve())]=digest(path)
    if kinds!={'exe','lib','pdb'} or seen!=replacements.keys():
        raise ValueError('Incomplete output/object redirection')
    command.append(jobs[1]['output'])
    write_json_new(destination/'build-plan.json',dict(cwd=str(build),compile=jobs,link=command,
        input_sha256=source_inputs,original_game_modules_recompiled=False))
    def compile_one(job):
        print('compile: '+Path(job['output']).name,flush=True)
        subprocess.run(job['command'],cwd=build,check=True)
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(compile_one,jobs))
    print('link private sdoj.exe',flush=True)
    subprocess.run(command,cwd=build,check=True)
    for path,expected in source_inputs.items():
        if digest(Path(path))!=expected:
            raise ValueError('Original source/object changed: '+path)
    write_json_new(destination/'build-result.json',dict(
        files={Path(p).name:digest(Path(p)) for p in [*[j['output'] for j in jobs],
            destination/'sdoj.exe',destination/'sdoj.pdb']},
        original_inputs_unchanged=True,original_modules_recompiled=False,release_certified=False))
    print('Private SDOJ host probe complete',flush=True)


if __name__=='__main__':
    main()
