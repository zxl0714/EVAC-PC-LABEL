"""Check title-specific native envelopes with authored, non-playable records."""
import argparse
import datetime
import json
from pathlib import Path
import struct
import subprocess
import sys
import uuid
import zlib

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from analysis_paths import private_output
from project import ROOT, digest, write_json_new
from ps4_build_receipt import verify
from replay_fixture import record


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    receipt=verify()
    directory=private_output(args.output or ROOT/'.local/ps4-replay-checks'/
        (datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ-')+uuid.uuid4().hex[:8]))
    directory.mkdir(parents=True,exist_ok=False)
    executable=ROOT/'out/ps4-native/KetsuiNativeProbe.exe'
    results=[]
    def check(name,game,data,valid,board=6,score=12345):
        source=directory/(name+'.attachment')
        with source.open('xb') as stream:
            stream.write(data)
        before=digest(source)
        result=subprocess.run([str(executable),'--community-replay-profile-check',game,str(source),str(board),str(score)],
            cwd=directory,capture_output=True,timeout=10,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        for suffix,content in [('stdout.jsonl',result.stdout),('stderr.txt',result.stderr)]:
            with (directory/(name+'.'+suffix)).open('xb') as stream:
                stream.write(content)
        events=[json.loads(line) for line in result.stdout.splitlines() if line.startswith(b'{')]
        passed=result.returncode==(0 if valid else 2) and len(events)==1 and events[0].get('valid_envelope') is valid and digest(source)==before
        if valid:
            passed=passed and events[0].get('game')==game and events[0].get('native_playback_validated') is False
        results.append(dict(check=name,passed=passed,exitCode=result.returncode))
        print(('PASS' if passed else 'FAIL')+': '+name,flush=True)
    djo=record('daioujou');kt=record('ketsui')
    base=bytearray(kt);struct.pack_into('<I',base,4,0x10000)
    check('ketsui-base','ketsui',zlib.compress(base),True,board=0)
    check('ketsui-base-not-daioujou','daioujou',zlib.compress(base),False)
    unknown_base=bytearray(base);struct.pack_into('<I',unknown_base,0,0x10000)
    check('ketsui-base-still-requires-container','ketsui',zlib.compress(unknown_base),False,board=0)
    unknown_base=bytearray(base);struct.pack_into('<I',unknown_base,4,0x10002)
    check('ketsui-unknown-data-version','ketsui',zlib.compress(unknown_base),False,board=0)
    for board in (60,61,62):
        custom=bytearray(kt);struct.pack_into('<H',custom,106,3)
        struct.pack_into('<H',custom,112,4 if board==62 else 0)
        check('ketsui-custom-'+str(board),'ketsui',zlib.compress(custom),True,board=board)
        check('ketsui-custom-not-arcade-'+str(board),'ketsui',zlib.compress(custom),False,board=0)
        check('ketsui-arcade-not-custom-'+str(board),'ketsui',zlib.compress(kt),False,board=board)
    check('daioujou-combined','daioujou',zlib.compress(djo),True)
    check('daioujou-character','daioujou',zlib.compress(djo),True,board=0)
    check('daioujou-local-save-is-not-network','daioujou',zlib.compress(djo[:0x65600]),False)
    native_alias=bytearray(djo);struct.pack_into('<H',native_alias,106,3)
    check('daioujou-native-arcade-alias','daioujou',zlib.compress(native_alias),True,board=0)
    check('daioujou-native-arcade-alias-combined','daioujou',zlib.compress(native_alias),True)
    check('daioujou-alias-not-custom-board','daioujou',zlib.compress(native_alias),False,board=16)
    check('daioujou-alias-not-premium','daioujou',zlib.compress(native_alias),False,board=36)
    for suffix in ('01000','01010','01020'):
        selected=bytearray(djo)
        selected[80:96]=b'ARCADE_'+suffix.encode()+bytes(4)
        check('daioujou-core-'+suffix,'daioujou',zlib.compress(selected),True)
    unknown=bytearray(djo);unknown[80:96]=b'ARCADE_99999'+bytes(4)
    check('daioujou-unknown-core','daioujou',zlib.compress(unknown),False)
    check('daioujou-wrong-character','daioujou',zlib.compress(djo),False,board=1)
    check('daioujou-score-units','daioujou',zlib.compress(djo),False,score=123450)
    check('daioujou-custom','daioujou',zlib.compress(djo),False,board=16)
    check('ketsui-legacy','ketsui',zlib.compress(kt),True,board=0)
    check('ketsui-is-not-daioujou','daioujou',zlib.compress(kt),False)
    check('daioujou-is-not-ketsui','ketsui',zlib.compress(djo),False,board=0)
    wrong=bytearray(djo);struct.pack_into('<I',wrong,4,0x10001)
    check('daioujou-wrong-core','daioujou',zlib.compress(wrong),False)
    wrong=bytearray(djo);struct.pack_into('<I',wrong,0,0x1000e)
    check('daioujou-legacy-not-assumed','daioujou',zlib.compress(wrong),False)
    check('daioujou-trailing-data','daioujou',zlib.compress(djo)+b'extra',False)
    write_json_new(directory/'result.json',dict(format=1,buildSourceCommit=receipt['sourceCommit'],
        buildSourceDirty=receipt['sourceDirty'],executableSha256=digest(executable),
        authoredDataOnly=True,nativeGameExecution=False,nativePlaybackValidated=False,checks=results))
    return 0 if all(item['passed'] for item in results) else 1


if __name__=='__main__':
    sys.exit(main())
