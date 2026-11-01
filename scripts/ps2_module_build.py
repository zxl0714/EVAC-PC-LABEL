"""Validate compiled stage inputs and emit their CMake/native registration glue."""
import argparse
import json
from pathlib import Path
import re
from ps2_translation_units import make_units, write_changed


def prepare(manifest, metadata, output):
    manifest,metadata,output=Path(manifest).resolve(),Path(metadata).resolve(),Path(output).resolve()
    spec=json.loads(manifest.read_text(encoding='utf-8'))
    declarations=json.loads(metadata.read_text(encoding='utf-8'))
    if spec.get('format')!=1 or declarations.get('format')!=1:
        raise ValueError('Unknown PS2 stage build manifest')
    known={module['id']:module for module in declarations['modules']}
    seen=set();configured=[]
    for source in spec['modules']:
        number=source['id']
        if type(number)!=int or not 1<=number<=15 or number in seen:
            raise ValueError('Invalid or duplicate stage module id')
        seen.add(number)
        directory=Path(source['generated']).resolve()
        receipt=json.loads(Path(source['receipt']).read_text(encoding='utf-8'))
        inputs=json.loads(Path(source['prepared']).read_text(encoding='utf-8'))
        original=known[number]
        prepared=next(m for m in inputs['modules'] if m['module_id']==number)
        summary=receipt['summary']
        if (inputs['bootSha256']!=declarations['bootSha256'] or
            prepared['sha256']!=original['sha256'] or prepared['analysisSha256']!=receipt['elfSha256'] or
            any(summary[key] for key in ('errors','decodeFailures','unhandledInstructions','criticalGuestFallbacks'))):
            raise ValueError('Stage generation does not match original module '+str(number))
        tag=f'{number:02}';prefix=f'g_ps2Stage{tag}FunctionTable'
        roots=re.findall(r'^void (root_[A-Za-z_0-9]+)\(',
                         (directory/'ps2_recompiled_functions.h').read_text(encoding='utf-8'),re.M)
        global_roots=source.get('globalRootAliases',False)
        if type(global_roots)!=bool:
            raise ValueError('Stage root alias mode must be boolean')
        # New banks can scope their entry names in the whole target, including
        # its PCH. This avoids reading every generated body to find references.
        # Existing banks retain source-specific aliases and their cached objects.
        units=make_units(directory,output/f'module-{tag}',symbols=() if global_roots else roots)
        configured.append(dict(number=number,tag=tag,prefix=prefix,directory=directory,
                               units=Path(units['manifest']),rootUsers=units['symbolUsers'],module=original,
                               globalRoots=roots if global_roots else []))
    if not configured:
        raise ValueError('No stage modules requested')
    output.mkdir(parents=True,exist_ok=True)
    cmake=''
    cpp='#include "ps2_runtime.h"\n#include "game_overrides.h"\n\n'
    for item in configured:
        prefix=item['prefix'];name='ps2_espgaluda_stage_'+item['tag']
        cmake+=f'include("{item["units"].as_posix()}")\n'
        cmake+=f'add_library({name} OBJECT ${{PS2_TRANSLATION_UNITS}})\n'
        cmake+=f'target_link_libraries({name} PRIVATE ps2_runtime)\n'
        cmake+=f'target_include_directories({name} PRIVATE "{item["directory"].as_posix()}")\n'
        cmake+=f'target_compile_definitions({name} PRIVATE\n'
        for suffix in ('','Base','End','SlotCount'):
            cmake+=f'  "g_ps2RecompiledFunctionTable{suffix}={prefix}{suffix}"\n'
        for symbol in item['globalRoots']:
            cmake+=f'  "{symbol}=espgaluda_stage_{item["tag"]}_{symbol}"\n'
        cmake+=')\n'
        cmake+=f'target_compile_options({name} PRIVATE /bigobj)\n'
        cmake+=f'set_target_properties({name} PROPERTIES UNITY_BUILD OFF)\n'
        cmake+=f'target_precompile_headers({name} PRIVATE "${{PS2_RUNTIME_INCLUDE}}/ps2_runtime.h"\n'
        cmake+=f'  "{item["directory"].as_posix()}/ps2_recompiled_functions.h" "{item["directory"].as_posix()}/ps2_recompiled_stubs.h")\n'
        cmake+=f'target_sources(ps2_espgaluda_probe PRIVATE $<TARGET_OBJECTS:{name}>)\n'
        # Scope ELF entry symbols in every unit that defines or references them.
        # Keep untouched body units and their PCH reusable across incremental builds.
        aliases={}
        for symbol,users in item['rootUsers'].items():
            for unit in users:
                aliases.setdefault(unit,[]).append(f'{symbol}=espgaluda_stage_{item["tag"]}_{symbol}')
        for unit,definitions in sorted(aliases.items()):
            cmake+=f'set_source_files_properties("{Path(unit).as_posix()}" PROPERTIES\n'
            cmake+='  COMPILE_DEFINITIONS "'+';'.join(sorted(definitions))+'" SKIP_PRECOMPILE_HEADERS ON)\n'
        cmake+=f'set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "{item["directory"].as_posix()}/register_functions.cpp"\n'
        cmake+=f'  "{item["directory"].as_posix()}/ps2_recompiled_functions.h")\n'
        cpp+=f'extern const uint32_t {prefix}Base;\nextern const uint32_t {prefix}SlotCount;\n'
        cpp+=f'extern PS2Runtime::RecompiledFunction {prefix}[];\n'
    cpp+='\nvoid RegisterEspgaludaStageModules(PS2Runtime& runtime) {\n'
    for item in configured:
        module=item['module'];prefix=item['prefix']
        fields=[item['number'],*(int(module[key],0) for key in ('address','textSize','dataSize','bssSize'))]
        cpp+='  runtime.registerStaticModule({'+','.join(hex(field) for field in fields)+','
        cpp+=prefix+'Base,{'+prefix+','+prefix+'SlotCount}});\n'
    cpp+='}\n'
    cpp+='PS2_REGISTER_GAME_OVERRIDE("ESPGaluda SLPS-25352 stage modules","",0x100008u,0x95b4197du,RegisterEspgaludaStageModules)\n'
    glue=output/'registration.cpp'
    cmake+=f'target_sources(ps2_espgaluda_probe PRIVATE "{glue.as_posix()}")\n'
    cmake+=f'set_source_files_properties("{glue.as_posix()}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)\n'
    write_changed(glue,cpp);write_changed(output/'modules.cmake',cmake)
    return dict(modules=len(configured),cmake=str(output/'modules.cmake'))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest',type=Path)
    parser.add_argument('--metadata',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    print(json.dumps(prepare(args.manifest,args.metadata,args.output)))
