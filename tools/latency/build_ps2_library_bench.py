"""Build a private ordinary/ThinLTO pair against the current PS2 runtime graph."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from project import digest,reject_link,write_json_new
from toolchain import tool_path


def properties(lines, contains):
    start=next(i for i,line in enumerate(lines) if line.startswith('build ') and contains in line)
    result={}
    for line in lines[start+1:]:
        if not line.strip(): break
        if line.startswith('  ') and ' = ' in line:
            key,value=line.strip().split(' = ',1);result[key]=value
    return result


def words(value):
    return [word.strip('"') for word in re.findall(r'"[^"]*"|\S+',value)]


def quoted(path):
    return json.dumps(str(path).replace('\\','/'))


def freeze_libraries(values, build, output):
    frozen=output/'runtime-libs';frozen.mkdir(exist_ok=True)
    libraries=[];identities={};origins=[]
    for value in words(values):
        candidate=build/value
        if candidate.is_file():
            reject_link(candidate)
            identity=digest(candidate)
            copied=frozen/(str(len(origins))+'-'+candidate.name)
            shutil.copyfile(candidate,copied)
            if digest(copied)!=identity: raise ValueError('Runtime library changed while freezing: '+value)
            libraries.append(quoted(copied));identities[str(copied)]=identity
            origins.append(dict(original=str(candidate),sha256=identity,frozen=str(copied)))
        elif re.fullmatch(r'[A-Za-z0-9_.+-]+\.lib',value): libraries.append(quoted(value))
        else: raise ValueError('Cannot resolve a current runtime link input: '+value)
    return libraries,identities,origins


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--jobs',type=int,choices=(1,2),default=2)
    parser.add_argument('--prepare-only',action='store_true')
    args=parser.parse_args()
    output=args.output.resolve();reject_link(output)
    if not output.is_relative_to(ROOT/'.local') or output.exists():
        raise ValueError('Use a fresh private .local output directory')
    output.mkdir(parents=True)
    build=ROOT/'out/ps2-tools'
    cache={}
    for line in (build/'CMakeCache.txt').read_text(encoding='utf8').splitlines():
        if line and not line.startswith(('#','//')) and ':' in line and '=' in line:
            key,value=line.split('=',1);cache[key.split(':',1)[0]]=value
    if cache.get('CMAKE_BUILD_TYPE')!='Release' or cache.get('PS2_RUNTIME_THINLTO')!='ON':
        raise ValueError('Build the Release PS2 runtime with ThinLTO first')
    if Path(cache['CMAKE_CXX_COMPILER']).resolve()!=tool_path('clang-cl',ROOT).resolve():
        raise ValueError('PS2 graph does not use the pinned compiler')
    lines=(build/'build.ninja').read_text(encoding='utf8').splitlines()
    executable=properties(lines,'espgaluda.exe:')
    runtime=properties(lines,'ps2_runtime.cpp.obj:')
    generated=Path(cache['PS2_GAME_GENERATED'])
    sources=[generated/(name+'.cpp') for name in ('FUN_00118048_0x118048',
        'FUN_00119c70_0x119c70','FUN_00119ee8_0x119ee8','FUN_0011a000_0x11a000')]
    sources.append(ROOT/'tools/latency/ps2_library_bench.cpp')
    includes=[generated]+[Path(value[2:]) for value in words(runtime['INCLUDES']) if value.startswith('-I')]
    definitions=[value[2:] for value in words(runtime['DEFINES']) if value.startswith('-D')]
    libraries,identities,origins=freeze_libraries(executable['LINK_LIBRARIES'],build,output)
    for source in sources: reject_link(source);identities[str(source)]=digest(source)
    contract=ROOT/'tests/ps2_libc_probe.cpp';identities[str(contract)]=digest(contract)
    cmake='\n'.join([
        'cmake_minimum_required(VERSION 3.25)','project(Ps2LibraryBench LANGUAGES CXX)',
        'set(CMAKE_CXX_STANDARD 20)','option(BENCH_THINLTO "Compile generated helpers with ThinLTO" OFF)',
        'add_executable(ps2_library_bench '+' '.join(map(quoted,sources))+')',
        'target_include_directories(ps2_library_bench PRIVATE '+' '.join(map(quoted,includes))+')',
        'target_compile_definitions(ps2_library_bench PRIVATE '+' '.join(map(quoted,definitions))+')',
        'target_compile_options(ps2_library_bench PRIVATE /O2 /Zi /EHsc /fp:precise /bigobj /utf-8)',
        'target_link_options(ps2_library_bench PRIVATE /STACK:8388608 /opt:lldltojobs=2)',
        'target_link_libraries(ps2_library_bench PRIVATE '+' '.join(libraries)+')',
        'if(BENCH_THINLTO)','  target_compile_options(ps2_library_bench PRIVATE /clang:-flto=thin)','endif()',''])
    (output/'CMakeLists.txt').write_text(cmake,encoding='utf8',newline='\n')
    shared=build/'bin/SDL3.dll';reject_link(shared)
    shutil.copyfile(shared,output/'SDL3.dll')
    identities[str(output/'SDL3.dll')]=digest(shared)
    if digest(output/'SDL3.dll')!=identities[str(output/'SDL3.dll')]:
        raise ValueError('SDL runtime changed while freezing')
    write_json_new(output/'inputs.json',dict(schema_version=1,inputs=identities,
        frozen_runtime_libraries=origins,
        cmake_sha256=digest(output/'CMakeLists.txt'),builder_sha256=digest(Path(__file__)),
        compiler=str(tool_path('clang-cl',ROOT)),compiler_sha256=digest(tool_path('clang-cl',ROOT)),
        scope='Original generated library helpers and current runtime, no window or disc',
        exclusions=['No game frame pacing, active IOP drivers or presentation cost is measured.']))
    if args.prepare_only:
        print(output);return
    for name,enabled in (('ordinary','OFF'),('thinlto','ON')):
        target=output/name
        with (output/(name+'-build.log')).open('xb') as log:
            subprocess.run([str(tool_path('cmake',ROOT)),'-S',str(output),'-B',str(target),'-G','Ninja',
                '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER='+str(tool_path('clang-cl',ROOT)),
                '-DCMAKE_MAKE_PROGRAM='+str(tool_path('ninja',ROOT)),'-DBENCH_THINLTO='+enabled],
                stdout=log,stderr=subprocess.STDOUT,check=True)
            subprocess.run([str(tool_path('cmake',ROOT)),'--build',str(target),'--parallel',str(args.jobs)],
                stdout=log,stderr=subprocess.STDOUT,check=True)
        binary=target/'ps2_library_bench.exe'
        shutil.copyfile(output/'SDL3.dll',target/'SDL3.dll')
        write_json_new(output/(name+'-binary.json'),dict(path=str(binary),sha256=digest(binary)))
    if any(digest(Path(path))!=identity for path,identity in identities.items()):
        raise ValueError('Runtime inputs changed during benchmark build; do not compare this pair')
    print(output)


if __name__=='__main__': main()
