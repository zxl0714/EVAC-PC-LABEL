"""Compare IOP dispatch candidates in a private frozen runtime pair."""
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


def nop_candidate(source):
    anchor='''    bool IopCpuCore::executeInstruction(IopCpuState &cpu, uint32_t instruction)
    {
'''
    if source.count(anchor)!=1:
        raise ValueError('IOP CPU instruction entry changed')
    fast='''        if (instruction == 0u)
        {
            const bool delaySlot = cpu.branchPending;
            cpu.pc = delaySlot ? cpu.branchTarget : cpu.pc + 4u;
            cpu.branchPending = false;
            if (!delaySlot) cpu.branchTarget = 0u;
            cpu.exception = false;
            cpu.yielded = false;
            if (cpu.pendingLoad)
            {
                if (cpu.pendingLoadReg != 0u)
                    cpu.gpr[cpu.pendingLoadReg] = cpu.pendingLoadValue;
                cpu.pendingLoad = false;
            }
            cpu.gpr[0] = 0u;
            return !cpu.stopped;
        }
'''
    return source.replace(anchor,anchor+fast)


def calendar_candidate(source):
    def replace(old,new,count=1):
        nonlocal source
        if source.count(old)!=count:
            raise ValueError('IOP service-dispatch source changed: '+old[:90])
        source=source.replace(old,new)
    replace('        uint64_t idleUntil = 0;',
            '        uint64_t idleUntil = 0;\n        uint64_t nextServiceCycle = 0;')
    source=source.replace('            idleUntil = 0;',
                          '            idleUntil = 0;\n            nextServiceCycle = 0;')
    source=source.replace('m_impl->idleUntil = 0;',
                          'm_impl->idleUntil = 0;\n        m_impl->nextServiceCycle = 0;')
    replace('''            if (import)
            {
                const ImportDisposition''','''            if (import)
            {
                nextServiceCycle = 0;
                const ImportDisposition''')
    replace('''            const bool running = cpuCore.executeInstruction(cpu, instruction);''','''            // A pending R3000 load can replace the base register when this
            // instruction retires. Classify the address using its original base.
            const uint32_t storeAddress = (instruction >> 26u) >= 0x28u
                ? physicalAddress(cpu.gpr[(instruction >> 21u) & 31u] +
                    static_cast<uint32_t>(static_cast<int16_t>(instruction))) : 0u;
            const bool running = cpuCore.executeInstruction(cpu, instruction);''')
    replace('''            if ((instruction >> 26u) >= 0x28u)
                schedulePendingDma();''','''            if ((instruction >> 26u) >= 0x28u)
            {
                if (storeAddress >= IopMemory::SifBase)
                    nextServiceCycle = 0;
                schedulePendingDma();
            }''')
    replace('''            while (const auto dma = memory.takeDmaStart())
                pendingDmaInterrupts[dma->irq] = totalCycles + dma->delayCycles;''','''            while (const auto dma = memory.takeDmaStart())
            {
                nextServiceCycle = 0;
                pendingDmaInterrupts[dma->irq] = totalCycles + dma->delayCycles;
            }''')
    replace('''                memory.advanceAudioTo(totalCycles);
                schedulePendingDma();''','''                nextServiceCycle = 0;
                memory.advanceAudioTo(totalCycles);
                schedulePendingDma();''')
    old='''                    serviceAudio();
                    servicePendingDmaInterrupts();
                    servicePendingGuestCallbacks();
                    timrman.serviceDue(totalCycles, *this);'''
    new='''                    if (totalCycles >= nextServiceCycle)
                    {
                        serviceAudio();
                        servicePendingDmaInterrupts();
                        servicePendingGuestCallbacks();
                        timrman.serviceDue(totalCycles, *this);
                        uint64_t deadline = memory.nextAudioCycle();
                        for (const auto &[irq, cycle] : pendingDmaInterrupts)
                            deadline = std::min(deadline, cycle);
                        if (!pendingGuestCallbacks.empty())
                            deadline = std::min(deadline, pendingGuestCallbacks.begin()->first);
                        nextServiceCycle = timrman.nextEventCycle(deadline);
                    }'''
    replace(old,new)
    return source


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--candidate',choices=('calendar','nop'),default='calendar')
    parser.add_argument('--run',action='store_true')
    args=parser.parse_args()
    output=args.output.resolve();reject_link(output)
    if not output.is_relative_to(ROOT/'.local') or output.exists():
        raise ValueError('Use a fresh private .local output directory')
    output.mkdir(parents=True)
    build=ROOT/'out/ps2-tools'
    lines=(build/'build.ninja').read_text(encoding='utf8').splitlines()
    link=properties(lines,'ps2_iop_memory_contract.exe:')
    compile=properties(lines,'ps2_iop_memory_test.cpp.obj:')
    libraries,identities,origins=freeze_libraries(link['LINK_LIBRARIES'],build,output)
    includes=[]
    for index,value in enumerate(words(compile['INCLUDES'])):
        if not value.startswith('-I'):continue
        original_include=Path(value[2:]);reject_link(original_include)
        copied=output/('include-'+str(index))
        shutil.copytree(original_include,copied)
        includes.append(copied)
        for path in sorted(copied.rglob('*')):
            if path.is_file():identities[str(path)]=digest(path)
    source=ROOT/'third_party/ps2/runtime'/('iop_cpu.cpp' if args.candidate=='nop' else 'iop_emulator.cpp')
    original=source.read_text(encoding='utf8')
    # Each pair replaces a complete translation unit. The frozen public ABI
    # and all other library inputs remain identical.
    variants={}
    transform=nop_candidate if args.candidate=='nop' else calendar_candidate
    for name,text in [('baseline',original),('candidate',transform(original))]:
        text=text.replace('../../../backends/ps2/',(ROOT/'backends/ps2').as_posix()+'/')
        path=output/(name+'.cpp');path.write_text(text,encoding='utf8',newline='\n')
        variants[name]=path
    test=output/'ps2_iop_memory_test.cpp'
    for path in (ROOT/'tests/ps2_iop_memory_test.cpp',ROOT/'tests/ps2_iop_audio_fixture.h'):
        shutil.copyfile(path,output/path.name)
    for path in (source,test,output/'ps2_iop_audio_fixture.h'):
        identities[str(path)]=digest(path)
    differential=[]
    if args.candidate=='nop':
        header=(ROOT/'third_party/ps2/runtime/iop_cpu.h').read_text(encoding='utf8')
        reference=output/'reference.cpp'
        reference.write_text(original.replace('"emulator/core/iop_cpu.h"','"cpu_reference.h"')
                             .replace('IopCpuCore','IopCpuReference'),encoding='utf8')
        definition=header[header.index('    class IopCpuCore'):header.rfind('}')]
        (output/'cpu_reference.h').write_text('#pragma once\n#include "emulator/core/iop_cpu.h"\n'
            'namespace ps2x::iop::detail {\n'+definition.replace('IopCpuCore','IopCpuReference')+'}\n',encoding='utf8')
        fixture=output/'differential.cpp'
        shutil.copyfile(ROOT/'tools/latency/ps2_iop_cpu_differential.cpp',fixture)
        differential=[fixture,reference,variants['candidate']]
    cmake=['cmake_minimum_required(VERSION 3.25)','project(Ps2IopDispatchBench LANGUAGES CXX)',
           'set(CMAKE_CXX_STANDARD 20)']
    targets={name:[test,path] for name,path in variants.items()}
    if differential:targets['differential']=differential
    for name,sources in targets.items():
        cmake += [f'add_executable({name} '+' '.join(map(quoted,sources))+')',
            f'target_include_directories({name} PRIVATE '+ ' '.join(map(quoted,includes))+')',
            f'target_compile_definitions({name} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)',
            f'target_compile_options({name} PRIVATE /O2 /Zi /EHsc /fp:precise /bigobj /utf-8 /clang:-flto=thin)',
            f'target_link_options({name} PRIVATE /STACK:8388608 /opt:lldltojobs=2)',
            f'target_link_libraries({name} PRIVATE '+' '.join(libraries)+')']
    (output/'CMakeLists.txt').write_text('\n'.join(cmake)+'\n',encoding='utf8')
    for path in output.glob('*'):
        if path.is_file():identities[str(path)]=digest(path)
    tool=tool_path('cmake',ROOT)
    subprocess.run([str(tool),'-S',str(output),'-B',str(output/'build'),'-G','Ninja',
        '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER='+str(tool_path('clang-cl',ROOT)),
        '-DCMAKE_MAKE_PROGRAM='+str(tool_path('ninja',ROOT))],check=True)
    subprocess.run([str(tool),'--build',str(output/'build'),'--parallel','2'],check=True)
    record=dict(scope='Private IOP '+args.candidate+' prototype; no production behavior changed',
        source_sha256=identities,library_origins=origins,variants={})
    if args.run:
        for name in targets:
            exe=output/'build'/(name+'.exe')
            result=subprocess.run([str(exe)],text=True,capture_output=True,check=True)
            record['variants'][name]=dict(exe_sha256=digest(exe),contract_stdout=result.stdout)
        record['runs']=[]
        for name in ('baseline','candidate','candidate','baseline'):
            for mode in ('--bench-dispatch','--bench-idle'):
                result=subprocess.run([str(output/'build'/(name+'.exe')),mode],
                                      text=True,capture_output=True,check=True)
                record['runs'].append(dict(variant=name,mode=mode,stdout=result.stdout))
    write_json_new(output/'comparison.json',record)
    print(json.dumps(record,indent=2))


if __name__=='__main__': main()
