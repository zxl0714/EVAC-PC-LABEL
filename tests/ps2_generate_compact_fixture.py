"""Generate baseline and shared-body code from an authored MIPS program."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from ps2_compact import prepare
from ps2_codegen import translation_summary
from ps2_module_extras import table_entries


def generate(tool, output):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    image = bytearray(0x300)
    struct.pack_into('<16sHHIIIIIHHHHHH', image, 0, b'\x7fELF\x01\x01\x01'+bytes(9),
                     2, 8, 1, 0x100000, 52, 0, 0x20924000, 52, 32, 1, 0, 0, 0)
    struct.pack_into('<8I', image, 52, 1, 0x80, 0x100000, 0x100000, 0x180, 0x180, 7, 16)
    routines = {0: [0x03e04021, 0x24020002, 0x0c040040, 0x24840001,
        0x24420003, 0x50a00003, 0x24420007, 0x2442000b, 0, 0x0100f821,
        0x03e00008, 0x00441821],
        0x40: [0x2484ffff, 0x1c80fffe, 0x24420001, 0x03e00008, 0],
        0x80: [0x03e04021, 0x3c190010, 0x37390100, 0x0320f809,
               0, 0x0100f821, 0x03e00008, 0],
        0x100: [0x24420005, 0x03e00008, 0]}
    for offset, words in routines.items():
        for index, word in enumerate(words):
            struct.pack_into('<I', image, 0x80 + offset + 4*index, word)
    (output/'input.elf').write_bytes(image)
    (output/'functions.csv').write_text('Name,Start,End,Size\n'
        'FUN_main,0x100000,0x100030,48\n'
        'entry_delay,0x10000c,0x100010,4\n'
        'entry_resume,0x100010,0x100030,32\n'
        'entry_restore,0x100024,0x100030,12\n'
        'FUN_loop,0x100040,0x100054,20\n'
        'entry_branch,0x100044,0x100054,16\n'
        'entry_loop_delay,0x100048,0x100054,12\n'
        'FUN_indirect,0x100080,0x1000a0,32\n'
        'entry_indirect_delay,0x100090,0x100094,4\n'
        'entry_indirect_resume,0x100094,0x1000a0,12\n'
        'FUN_leaf,0x100100,0x10010c,12\n'
        'entry_leaf_return,0x100104,0x10010c,8\n', encoding='utf8')
    config = dict(input=(output/'input.elf').as_posix(),
        output=(output/'baseline').as_posix(), ghidra_output=(output/'functions.csv').as_posix(),
        single_file_output=False, output_worker_threads=1, stubs=[], skip=[],
        patch_syscalls=False, patch_cop0=True, patch_cache=True)
    path = output/'baseline.toml'
    path.write_text('[general]\n'+''.join(k+' = '+json.dumps(v)+'\n' for k,v in config.items()))
    import ps2_compact
    fingerprint=hashlib.sha256(path.read_bytes()+(output/'input.elf').read_bytes()+
        (output/'functions.csv').read_bytes()+Path(ps2_compact.__file__).read_bytes()).hexdigest()[:16]
    compact_dir = output/('compact-'+fingerprint)
    if compact_dir.exists():
        compact_config = compact_dir/'config.toml'
    else:
        config['output'] = (output/'compact').as_posix()
        (output/'candidate.toml').write_text('[general]\n'+''.join(k+' = '+json.dumps(v)+'\n' for k,v in config.items()))
        compact_config = Path(prepare(output/'candidate.toml', compact_dir)['config'])
    for label, configuration in (('baseline', path), ('compact', compact_config)):
        run = subprocess.run([str(tool), str(configuration)], capture_output=True, text=True, timeout=30)
        (output/(label+'.log')).write_text(run.stdout+run.stderr, encoding='utf8')
        if run.returncode:
            raise RuntimeError(run.stderr)
        translation_summary(run.stdout)
    combined = '#include "ps2_runtime.h"\n#include "ps2_runtime_macros.h"\n#include "ps2_syscalls.h"\n#include "ps2_stubs.h"\n'
    tables = {}
    for label in ('baseline', 'compact'):
        directory = output/label
        entries = table_entries(directory/'register_functions.cpp')
        tables[label] = entries
        combined += 'namespace '+label+' {\n'
        for name in sorted(set(entries.values())):
            combined += 'void '+name+'(uint8_t*,R5900Context*,PS2Runtime*);\n'
        for source in sorted(directory.glob('*.cpp')):
            if source.name != 'register_functions.cpp':
                combined += re.sub(r'^#include .*\n', '', source.read_text(), flags=re.M)+'\n'
        combined += 'const FixtureEntry entries[]{\n'
        for pc, name in sorted(entries.items()):
            combined += f'{{0x{pc:x}u,{name}}},\n'
        combined += '};\n}\n'
    if tables['baseline'].keys() != tables['compact'].keys():
        raise ValueError('Compaction changed the authored callable address set')
    (output/'fixture.h').write_text(combined, encoding='utf8', newline='\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    generate(args.tool, args.output)
