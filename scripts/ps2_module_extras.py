"""Compile verified additional module entries while retaining cached native bodies."""
import argparse
import csv
import json
from pathlib import Path
import re
from project import digest
from ps2_translation_units import write_changed


def table_entries(path):
    text=Path(path).read_text(encoding='utf-8')
    pattern=r'g_ps2RecompiledFunctionTable\[\d+\] = (\w+); // 0x([0-9a-f]+)'
    return {int(pc,16):name for name,pc in re.findall(pattern,text)}


def prepare_one(spec, output):
    if spec.get('format')!=1 or spec.get('id') not in (0,16,17):
        raise ValueError('Expected ESPGaluda boot or mode additional entries')
    mode={0:'boot',16:'arcade',17:'arrange'}[spec['id']]
    target='ps2_espgaluda_'+mode+'_extras'
    base,generated=Path(spec['base']).resolve(),Path(spec['generated']).resolve()
    function_map=Path(spec['map']).resolve()
    receipt=json.loads(Path(spec['receipt']).read_text(encoding='utf-8'))
    previous=json.loads(Path(spec['baseReceipt']).read_text(encoding='utf-8'))
    for value in (receipt,previous):
        if any(value['summary'][key] for key in
               ('errors','decodeFailures','unhandledInstructions','criticalGuestFallbacks')):
            raise ValueError('Additional entries need valid code generation')
    if receipt['elfSha256']!=previous['elfSha256'] or receipt['functionMapSha256']!=digest(function_map):
        raise ValueError('Additional entries do not match the original module image and map')
    with function_map.open(encoding='utf-8',newline='') as stream:
        rows=list(csv.DictReader(stream))
    original=table_entries(base/'register_functions.cpp')
    base_entries={int(pc,0) for pc in spec.get('baseEntries',[])}
    if base_entries-set(original) or base_entries-{int(row['Start'],0) for row in rows}:
        raise ValueError('Generation entry anchors must already be linked in the base table')
    anchors={row['Name']+'_0x'+format(int(row['Start'],0),'x')
             for row in rows if int(row['Start'],0) in base_entries}
    rows=[row for row in rows if int(row['Start'],0) not in base_entries]
    symbols={row['Name']+'_0x'+format(int(row['Start'],0),'x') for row in rows}
    if not symbols or len(symbols)!=len(rows):
        raise ValueError('Expected unique additional functions')
    sources=[generated/(name+'.cpp') for name in sorted(symbols)]
    for source in sources:
        if not source.is_file() or source.is_symlink():
            raise ValueError('Missing regular additional function body')
        if re.search(r'\broot_\w+',source.read_text(encoding='utf-8')):
            raise ValueError('Additional function needs explicit ELF-root linkage review')
        if anchors and re.search(r'\b(?:'+'|'.join(map(re.escape,anchors))+r')\b',source.read_text(encoding='utf-8')):
            raise ValueError('Additional function directly references a generation entry anchor')
    entries={pc:name for pc,name in table_entries(generated/'register_functions.cpp').items() if name in symbols}
    if (set(entries.values())!=symbols or original.keys()&entries.keys() or
        any(int(row['Start'],0) not in entries for row in rows)):
        raise ValueError('Additional functions overlap or are missing from dispatch tables')
    # Generation contains ELF-symbol roots too; only the explicit map's new
    # bodies and their resumable PCs belong in this extension.
    output=Path(output).resolve();output.mkdir(parents=True,exist_ok=True)
    prefix={0:'g_ps2RecompiledFunctionTable',16:'g_ps2ArcadeFunctionTable',17:'g_ps2ArrangeFunctionTable'}[spec['id']]
    code='#include "ps2_runtime.h"\n#include "game_overrides.h"\n#include <stdexcept>\n'
    code+=f'extern const uint32_t {prefix}Base, {prefix}SlotCount;\n'
    code+=f'extern PS2Runtime::RecompiledFunction {prefix}[];\n'
    for symbol in sorted(symbols):
        code+=f'void {symbol}(uint8_t*,R5900Context*,PS2Runtime*);\n'
    code+='namespace {\nvoid Install(PS2Runtime&) {\n'
    code+='  struct Entry {uint32_t pc;PS2Runtime::RecompiledFunction function;};\n'
    code+='  const Entry entries[]{\n'
    for pc,symbol in sorted(entries.items()):code+=f'    {{0x{pc:x}u,{symbol}}},\n'
    code+='  };\n  for(const auto& entry:entries) {\n'
    code+=f'    if(entry.pc<{prefix}Base || ((entry.pc-{prefix}Base)&3) ||\n'
    code+=f'       (entry.pc-{prefix}Base)/4>={prefix}SlotCount)\n'
    code+='      throw std::runtime_error("Additional PS2 function is outside its module table");\n'
    code+=f'    const auto previous={prefix}[(entry.pc-{prefix}Base)/4];\n'
    code+='    if(previous && previous!=entry.function)\n'
    code+='      throw std::runtime_error("Additional PS2 function overlaps compiled code");\n  }\n'
    code+=f'  for(const auto& entry:entries){prefix}[(entry.pc-{prefix}Base)/4]=entry.function;\n'
    code+=f'}}\n}}\nPS2_REGISTER_GAME_OVERRIDE("ESPGaluda {mode} additional entries","",0x100008u,0x95b4197du,Install)\n'
    registration=output/'registration.cpp';write_changed(registration,code)
    cmake=f'add_library({target} OBJECT\n'
    cmake+=''.join(f'  "{source.as_posix()}"\n' for source in sources)+')\n'
    cmake+=f'target_link_libraries({target} PRIVATE ps2_runtime)\n'
    cmake+=f'target_include_directories({target} PRIVATE "{generated.as_posix()}")\n'
    cmake+=f'set_target_properties({target} PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 256)\n'
    cmake+=f'target_precompile_headers({target} PRIVATE "${{PS2_RUNTIME_INCLUDE}}/ps2_runtime.h")\n'
    cmake+=f'target_sources(ps2_espgaluda_probe PRIVATE $<TARGET_OBJECTS:{target}>\n'
    cmake+=f'  "{registration.as_posix()}")\n'
    cmake+=f'set_source_files_properties("{registration.as_posix()}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON SKIP_PRECOMPILE_HEADERS ON)\n'
    write_changed(output/'extras.cmake',cmake)
    return dict(functions=len(symbols),entries=len(entries),cmake=str(output/'extras.cmake'))


def prepare(manifest, output):
    spec=json.loads(Path(manifest).read_text(encoding='utf-8'))
    if 'modules' not in spec:
        return prepare_one(spec,output)
    modules=spec['modules']
    if spec.get('format')!=1 or not modules or len({m['id'] for m in modules})!=len(modules):
        raise ValueError('Expected unique additional module inventories')
    output=Path(output).resolve();output.mkdir(parents=True,exist_ok=True)
    results=[prepare_one(dict(module,format=1),output/str(module['id'])) for module in modules]
    write_changed(output/'extras.cmake',''.join('include("'+Path(r['cmake']).as_posix()+'")\n' for r in results))
    return dict(modules=len(results),functions=sum(r['functions'] for r in results),
                entries=sum(r['entries'] for r in results),cmake=str(output/'extras.cmake'))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest',type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    print(json.dumps(prepare(args.manifest,args.output)))
