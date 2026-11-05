"""Freeze Vulkan/DXGI WSI variants; leave production dependencies and binaries untouched."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from project import digest,reject_link,write_json_new
from toolchain import tool_path
from build_ps2_library_bench import properties,words,quoted,freeze_libraries


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();out=args.output.resolve();reject_link(out)
    if out.exists() or not out.is_relative_to(ROOT/'.local'):raise ValueError('Use a new private .local directory')
    out.mkdir(parents=True)
    build=ROOT/'out/ps2-gs';lines=(build/'build.ninja').read_text().splitlines()
    graphic=properties(lines,'ps2_gs_device.dir')
    wsi=properties(lines,'granite-vulkan.dir')
    executable=properties(lines,'build ps2_gs_window_probe.exe:')
    cache=dict(line.split('=',1) for line in (build/'CMakeCache.txt').read_text().splitlines()
               if line.startswith('PS2_GS_SOURCE:'))
    source=Path(next(iter(cache.values())))
    reject_link(source)
    includes=list(dict.fromkeys(value[2:] for cfg in (graphic,wsi)
                               for value in words(cfg['INCLUDES']) if value.startswith('-I')))
    defines=list(dict.fromkeys(value[2:] for cfg in (graphic,wsi)
                              for value in words(cfg['DEFINES']) if value.startswith('-D')))
    includes.append(str(ROOT/'tools/ps2-gs'))
    libraries,identities,origins=freeze_libraries(executable['LINK_LIBRARIES'],build,out)
    for name in ('wsi.cpp','wsi_dxgi.cpp'):
        src=source/'Granite/vulkan'/name;reject_link(src);shutil.copyfile(src,out/name)
        identities[str(out/name)]=digest(out/name)
    shutil.copyfile(ROOT/'backends/ps2/gs_display.cpp',out/'gs_display.cpp')
    shutil.copyfile(ROOT/'tools/ps2-gs/wsi_probe.cpp',out/'probe.cpp')
    # The copied probe's relative common-header include is replaced explicitly.
    probe=(out/'probe.cpp').read_text().replace('"../../common/windows_timer_resolution.h"',
        quoted(ROOT/'common/windows_timer_resolution.h'))
    (out/'probe.cpp').write_text(probe,encoding='utf8',newline='\n')
    cmake=['cmake_minimum_required(VERSION 3.25)','project(Ps2WsiBench LANGUAGES CXX)',
           'set(CMAKE_CXX_STANDARD 20)','set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)']
    for name in ('vulkan','dxgi'):
        cmake.extend([f'add_executable({name} probe.cpp gs_display.cpp wsi.cpp'+(' wsi_dxgi.cpp' if name=='dxgi' else '')+')',
            f'target_include_directories({name} PRIVATE '+' '.join(map(quoted,includes))+')',
            f'target_compile_definitions({name} PRIVATE '+' '.join(defines)+(' HAVE_WSI_DXGI_INTEROP' if name=='dxgi' else '')+')',
            f'target_compile_options({name} PRIVATE /O2 /Zi /EHsc /fp:precise /utf-8 /bigobj)',
            f'target_link_options({name} PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)',
            f'target_link_libraries({name} PRIVATE '+' '.join(libraries)+' winmm.lib)'])
    (out/'CMakeLists.txt').write_text('\n'.join(cmake)+'\n',encoding='utf8')
    for name in ('gs_display.cpp','probe.cpp','CMakeLists.txt'):identities[str(out/name)]=digest(out/name)
    write_json_new(out/'inputs.json',dict(scope='Private authored graphics workload, no ROM or original game CPU',
        production_adopted=False,same_sampled_path=True,inputs=identities,
        libraries=origins,source_view_receipt=digest(source/'collection-source.json'),
        builder_sha256=digest(Path(__file__))))
    tool=tool_path('cmake',ROOT)
    subprocess.run([str(tool),'-S',str(out),'-B',str(out/'build'),'-G','Ninja',
        '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER='+str(tool_path('clang-cl',ROOT)),
        '-DCMAKE_MAKE_PROGRAM='+str(tool_path('ninja',ROOT))],check=True)
    subprocess.run([str(tool),'--build',str(out/'build'),'--parallel','2'],check=True)
    print(json.dumps(dict(output=str(out),executables={n:digest(out/'build'/(n+'.exe')) for n in ('vulkan','dxgi')})))


if __name__=='__main__':main()
