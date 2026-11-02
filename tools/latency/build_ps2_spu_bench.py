"""Freeze the current DSP and compare a static-silent-channel scheduling probe."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys

from build_ps2_library_bench import ROOT,properties,quoted,freeze_libraries
sys.path.insert(0,str(ROOT/'scripts'))
from project import digest,reject_link,write_json_new
from toolchain import tool_path
from ps2_spu_sources import prepare


def compare(output,blocks):
    runs=[]
    for index,variant in enumerate(('baseline','candidate','candidate','baseline')):
        directory=output/('run-'+str(index)+'-'+variant)
        binary=output/('bin-'+variant)/'ps2_spu_bench.exe'
        identity=json.loads((output/(variant+'-binary.json')).read_text())['sha256']
        if digest(binary)!=identity: raise ValueError('DSP benchmark EXE changed')
        result=subprocess.run([str(binary),str(directory),str(blocks)],capture_output=True,text=True)
        log=output/('run-'+str(index)+'.log')
        log.write_text(result.stdout+'\nSTDERR\n'+result.stderr,encoding='utf8')
        if result.returncode: raise RuntimeError('DSP contracts failed: '+result.stderr[-2000:])
        rows=[json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
        if len(rows)!=8: raise ValueError('DSP scenarios missing')
        runs.append(dict(variant=variant,rows=rows,log_sha256=digest(log),binary_sha256=identity))
    comparisons=[]
    for scenario in range(8):
        rows=[run['rows'][scenario] for run in runs]
        for key in ('pcm_hash','register_hash','irq_hash','audio_frames'):
            if len({row[key] for row in rows})!=1: raise ValueError('DSP mismatch: '+str(scenario)+' '+key)
        buffers=[(output/('run-'+str(i)+'-'+run['variant'])/('scenario-'+str(scenario)+'.pcm')).read_bytes()
                 for i,run in enumerate(runs)]
        if any(data!=buffers[0] for data in buffers): raise ValueError('DSP PCM byte mismatch')
        values={v:[run['rows'][scenario]['ms_per_audio_second'] for run in runs if run['variant']==v]
                for v in ('baseline','candidate')}
        means={v:statistics.mean(samples) for v,samples in values.items()}
        comparisons.append(dict(scenario=scenario,voices=rows[0]['voices'],**means,samples=values,
            reduction_percent=(1-means['candidate']/means['baseline'])*100,
            pcm_sha256=hashlib.sha256(buffers[0]).hexdigest()))
    write_json_new(output/'comparison.json',dict(blocks=blocks,runs=runs,comparison=comparisons,
        inputs_sha256=digest(output/'inputs.json'),scope='DSP Render only; no game CPU or frame claim'))
    print(json.dumps(comparisons,indent=2))




def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--run',action='store_true',help='Run byte-exact PCM/state ABBA comparison after compiling')
    parser.add_argument('--blocks',type=int,default=16000,choices=range(128,48001),metavar='128..48000')
    args=parser.parse_args()
    output=args.output.resolve();reject_link(output)
    if not output.is_relative_to(ROOT/'.local') or output.exists():
        raise ValueError('Use a fresh private .local output directory')
    output.mkdir(parents=True)
    build=ROOT/'out/ps2-tools'
    lines=(build/'build.ninja').read_text(encoding='utf8').splitlines()
    props=properties(lines,'ps2_spu_contract.exe:')
    libraries,identities,origins=freeze_libraries(props['LINK_LIBRARIES'],build,output)
    prepare(ROOT/'.deps/ps2',output/'baseline',static_channel_optimization=False)
    prepare(ROOT/'.deps/ps2',output/'candidate',static_channel_optimization=True)
    source=ROOT/'tools/latency/ps2_spu_bench.cpp'
    device=ROOT/'backends/ps2/spu_device.cpp'
    for path in [source,device,ROOT/'tests/ps2_spu_test.cpp',ROOT/'scripts/ps2_spu_sources.py']+list((output/'baseline').rglob('*'))+list((output/'candidate').rglob('*')):
        if path.is_file(): identities[str(path)]=digest(path)
    cmake='\n'.join(['cmake_minimum_required(VERSION 3.25)','project(Ps2SpuBench LANGUAGES CXX)',
        'set(CMAKE_CXX_STANDARD 20)','set(SPU_VARIANT "baseline" CACHE STRING "Frozen DSP source variant")',
        'set(dsp "${CMAKE_CURRENT_LIST_DIR}/${SPU_VARIANT}")',
        'add_executable(ps2_spu_bench '+quoted(source)+' '+quoted(device)+
        ' "${dsp}/src/Iop_SpuBase.cpp" "${dsp}/src/Iop_Spu2_Core.cpp" "${dsp}/src/Iop_Spu2.cpp")',
        'target_include_directories(ps2_spu_bench PRIVATE "${dsp}/include" '+quoted(ROOT/'backends/ps2')+')',
        'target_compile_definitions(ps2_spu_bench PRIVATE LOGGING_ENABLED=0)',
        'target_compile_options(ps2_spu_bench PRIVATE /O2 /Zi /EHsc /fp:precise /utf-8 /clang:-fno-strict-aliasing /clang:-flto=thin)',
        'target_link_options(ps2_spu_bench PRIVATE /STACK:8388608 /opt:lldltojobs=2)',
        'target_link_libraries(ps2_spu_bench PRIVATE '+' '.join(libraries)+')',''])
    (output/'CMakeLists.txt').write_text(cmake,encoding='utf8',newline='\n')
    write_json_new(output/'inputs.json',dict(inputs=identities,frozen_libraries=origins,
        builder_sha256=digest(Path(__file__)),cmake_sha256=digest(output/'CMakeLists.txt'),
        scope='Authored SPU PCM and register comparison, no game or firmware'))
    for variant in ('baseline','candidate'):
        target=output/('bin-'+variant)
        with (output/(variant+'-build.log')).open('xb') as log:
            subprocess.run([str(tool_path('cmake',ROOT)),'-S',str(output),'-B',str(target),'-G','Ninja',
                '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER='+str(tool_path('clang-cl',ROOT)),
                '-DCMAKE_MAKE_PROGRAM='+str(tool_path('ninja',ROOT)),'-DSPU_VARIANT='+variant],
                stdout=log,stderr=subprocess.STDOUT,check=True)
            subprocess.run([str(tool_path('cmake',ROOT)),'--build',str(target),'--parallel','1'],
                stdout=log,stderr=subprocess.STDOUT,check=True)
        binary=target/'ps2_spu_bench.exe'
        write_json_new(output/(variant+'-binary.json'),dict(path=str(binary),sha256=digest(binary)))
    if any(digest(Path(path))!=identity for path,identity in identities.items()):
        raise ValueError('DSP benchmark inputs changed during compilation')
    if args.run: compare(output,args.blocks)
    print(output)


if __name__=='__main__': main()
