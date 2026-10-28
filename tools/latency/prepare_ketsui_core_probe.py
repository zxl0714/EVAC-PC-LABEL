"""Add opcode-pinned Ketsui core observations to existing private PS4 copies."""
import argparse
import json
from pathlib import Path
import shutil
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from project import digest, write_json_new
from prepare_ps4_native_probe import replace_one


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base',type=Path,required=True)
    parser.add_argument('--audit',type=Path,required=True)
    parser.add_argument('--destination',type=Path,required=True)
    args=parser.parse_args()
    base,audit_path,destination=(p.resolve() for p in (args.base,args.audit,args.destination))
    private=ROOT/'.local/input-latency-v080-r1'
    if not all(p.is_relative_to(private) for p in (base,audit_path,destination)) or destination.exists():
        raise ValueError('Verified private base/audit and fresh output required')
    audit=json.loads(audit_path.read_text())
    if (audit['guest_execution'] or audit['original_data_modified'] or
        (audit['frame_slot'],audit['frame_callback'])!=(0x50088,0x6610) or
        audit['imports']['deliver_input']['provider_offset']!=0x5c0a0):
        raise ValueError('Original core/input audit differs')
    source_hashes={Path(s['path']).name:s['sha256'] for s in audit['sources']}
    for source in audit['sources']:
        if digest(Path(source['path']))!=source['sha256']:
            raise ValueError('Original audit input changed')
    prior=json.loads((base/'preparation.json').read_text())
    destination.mkdir()
    copied=[]
    for item in prior['files']:
        if digest(Path(item['source']))!=item['original_sha256'] or digest(Path(item['copy']))!=item['copy_sha256']:
            raise ValueError('Private base or original source changed')
        target=destination/Path(item['copy']).name
        shutil.copy2(item['copy'],target)
        if target.name=='native_probe.cpp':
            text=target.read_text(encoding='utf-8')
            text=replace_one(text,'void RebindImports(Runtime& runtime) {',
                '#include "ps4_ketsui_core_probe.h"\n\nvoid RebindImports(Runtime& runtime) {')
            text=replace_one(text,'        const auto target = GuestExport(import); if (!target) { continue; }',
                '        const auto original_target = GuestExport(import); if (!original_target) { continue; }\n'
                '        const auto target = ketsui_core_probe::WrapImport(runtime,import,original_target);')
            text=replace_one(text,'        import.address = GuestExport(import);\n        if (import.address) { continue; }',
                '        import.address = GuestExport(import);\n'
                '        if (import.address) {\n'
                '            import.address = ketsui_core_probe::WrapImport(runtime,import,import.address);\n'
                '            continue;\n        }')
            text=replace_one(text,'    reader.End();\n    if (!runtime.is_module) {',
                '    reader.End();\n    ketsui_core_probe::Install(runtime);\n    if (!runtime.is_module) {')
            target.write_text(text,encoding='utf-8')
        copied.append(dict(item,copy=str(target),copy_sha256=digest(target)))
    header=ROOT/'tools/latency/ps4_ketsui_core_probe.h'
    shutil.copy2(header,destination/header.name)
    copied.append(dict(source=str(header),original_sha256=digest(header),
        copy=str(destination/header.name),copy_sha256=digest(destination/header.name)))
    pins=destination/'ketsui-probe-pins.h'
    def literal(value):
        return ','.join('0x'+value[i:i+2] for i in range(0,64,2))
    pins.write_text('#pragma once\n' + ''.join(
        f'constexpr std::array<unsigned char,32> {name}{{{literal(source_hashes[file])}}};\n'
        for name,file in (('kKetsuiModuleSha','ketsui_01000.prx'),
                          ('kKetsuiIntegrationSha','emu_integration_01400.prx'))),encoding='ascii')
    copied.append(dict(source=str(audit_path),original_sha256=digest(audit_path),
        copy=str(pins),copy_sha256=digest(pins)))
    write_json_new(destination/'preparation.json',dict(files=copied,private_only=True,
        base_preparation_sha256=digest(base/'preparation.json'),audit_sha256=digest(audit_path),
        original_code_instructions_modified=False,generator_sha256=digest(Path(__file__))))
    print(destination)


if __name__=='__main__':
    main()
