"""Generate native code for an authored direct-call SDK binding fixture."""
import argparse
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from ps2_codegen import translation_summary


def generate(tool,output):
    output=Path(output).resolve();output.mkdir(parents=True,exist_ok=True)
    elf=bytearray(0x200)
    struct.pack_into('<16sHHIIIIIHHHHHH',elf,0,b'\x7fELF\x01\x01\x01'+bytes(9),
                     2,8,1,0x100000,52,0,0x20924000,52,32,1,0,0,0)
    struct.pack_into('<8I',elf,52,1,0x80,0x100000,0x100000,0x110,0x110,7,16)
    # A tail jump uses the generated C++ entry directly, bypassing the runtime
    # dispatch table. The original fixture returns a sentinel, not memset.
    caller=[0x08040040,0]
    for index,word in enumerate(caller):struct.pack_into('<I',elf,0x80+index*4,word)
    for index,word in enumerate([0x34020bad,0x03e00008,0]):
        struct.pack_into('<I',elf,0x180+index*4,word)
    (output/'input.elf').write_bytes(elf)
    (output/'functions.csv').write_text('Name,Start,End,Size\n'
        'FUN_00100000,0x00100000,0x00100008,8\n'
        'FUN_00100100,0x00100100,0x0010010C,12\n',encoding='utf-8')
    config='[general]\noutput_worker_threads = 1\n'
    config+=f'input = "{(output/"input.elf").as_posix()}"\n'
    config+=f'output = "{(output/"generated").as_posix()}"\n'
    config+=f'ghidra_output = "{(output/"functions.csv").as_posix()}"\n'
    config+='single_file_output = false\npatch_syscalls = false\npatch_cop0 = true\npatch_cache = true\n'
    config+='stubs = ["memset@0x00100100"]\nuntracked_stubs = []\nskip = []\n'
    (output/'config.toml').write_text(config,encoding='utf-8')
    result=subprocess.run([str(tool),str(output/'config.toml')],capture_output=True,text=True,timeout=30)
    (output/'generation.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    if result.returncode:raise RuntimeError(result.stderr)
    summary=translation_summary(result.stdout)
    if summary['functions']!=2 or summary['stubs']!=1:
        raise RuntimeError('Direct SDK fixture did not retain its binding')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();generate(args.tool,args.output)
