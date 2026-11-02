"""Freeze and compare a module-range fast rejection candidate without game data."""
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


def candidate(source,header):
    edits=[('  modules_.push_back(module);','''  modules_.push_back(module);
  firstAddress_=std::min(firstAddress_,uint32_t(textBegin));
  lastAddress_=std::max(lastAddress_,uint32_t(textEnd));'''),
        ('  if(address&3)return nullptr;',
         '  if((address&3) || address<firstAddress_ || address>=lastAddress_)return nullptr;'),
        ('void StaticModules::clear() {modules_.clear();memory_={};}',
         'void StaticModules::clear() {modules_.clear();memory_={};firstAddress_=UINT32_MAX;lastAddress_=0;}')]
    source=source.replace('#include <array>','#include <array>\n#include <algorithm>')
    for before,after in edits:
        if source.count(before)!=1:raise ValueError('Module selection source changed')
        source=source.replace(before,after)
    before='  std::vector<StaticModule> modules_;'
    if header.count(before)!=1:raise ValueError('Module selection layout changed')
    header=header.replace(before,before+'\n  uint32_t firstAddress_=UINT32_MAX,lastAddress_=0;')
    return source,header


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();output=args.output.resolve();reject_link(output)
    if not output.is_relative_to(ROOT/'.local') or output.exists():
        raise ValueError('Use a new private .local directory')
    output.mkdir(parents=True)
    original=(ROOT/'backends/ps2/static_modules.cpp').read_text()
    header=(ROOT/'backends/ps2/static_modules.h').read_text()
    modified,definition=candidate(original,header)
    test=ROOT/'tests/ps2_modules_test.cpp'
    for name,source,h in [('baseline',original,header),('candidate',modified,definition)]:
        path=output/name;path.mkdir()
        (path/'static_modules.cpp').write_text(source,encoding='utf8',newline='\n')
        (path/'static_modules.h').write_text(h,encoding='utf8',newline='\n')
        shutil.copyfile(test,path/'contract.cpp')
    cmake=['cmake_minimum_required(VERSION 3.25)','project(Ps2ModuleBench LANGUAGES CXX)',
           'set(CMAKE_CXX_STANDARD 20)']
    for name in ('baseline','candidate'):
        cmake.extend([f'add_executable({name} {name}/static_modules.cpp {name}/contract.cpp)',
            f'target_include_directories({name} PRIVATE "${{CMAKE_CURRENT_SOURCE_DIR}}/{name}")',
            f'target_compile_options({name} PRIVATE /O2 /Zi /EHsc /fp:precise /utf-8)'])
    (output/'CMakeLists.txt').write_text('\n'.join(cmake)+'\n',encoding='utf8')
    tool=tool_path('cmake',ROOT)
    subprocess.run([str(tool),'-S',str(output),'-B',str(output/'build'),'-G','Ninja',
        '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER='+str(tool_path('clang-cl',ROOT)),
        '-DCMAKE_MAKE_PROGRAM='+str(tool_path('ninja',ROOT))],check=True)
    subprocess.run([str(tool),'--build',str(output/'build'),'--parallel','2'],check=True)
    contracts={}
    for name in ('baseline','candidate'):
        exe=output/'build'/(name+'.exe')
        result=subprocess.run([str(exe)],text=True,capture_output=True,check=True)
        contracts[name]=dict(stdout=result.stdout,executable_sha256=digest(exe))
    runs=[]
    for name in ('baseline','candidate','candidate','baseline')*3:
        result=subprocess.run([str(output/'build'/(name+'.exe')),'--bench'],text=True,capture_output=True,check=True)
        runs.append(dict(variant=name,rows=[json.loads(line) for line in result.stdout.splitlines()]))
    identities={str(p):digest(p) for p in sorted(output.glob('*/*')) if p.is_file() and p.suffix in ('.cpp','.h')}
    record=dict(scope='Authored module lookup only; not whole game CPU',contracts=contracts,runs=runs,sources=identities,
        production_adopted=False,source_sha256=digest(Path(__file__)))
    write_json_new(output/'comparison.json',record)
    print(json.dumps(record,indent=2))


if __name__=='__main__':main()
