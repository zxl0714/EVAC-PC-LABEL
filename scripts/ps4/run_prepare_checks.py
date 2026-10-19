"""Differential-check the compiled ELF preparer using authored, ROM-free inputs."""
import argparse
import datetime
import json
from pathlib import Path
import subprocess
import sys
import uuid

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from analysis_paths import private_output
from project import ROOT,digest,write_json_new
from ps4_build_receipt import verify
sys.path.insert(0,str(ROOT/'scripts/ps4'))
sys.path.insert(0,str(ROOT/'tests'))
import prepare_native
import ps4_elf_fixtures as fixtures


def path_checks(executable,directory,inputs):
    results=[]
    for name in ('existing','wrong-place','source-overlap','junction','alternate-stream',
                 'other-game-overlap','other-dlc-overlap','invalid-sibling','relative-component'):
        root=directory/('collection-'+name);root.mkdir();(root/'.local').mkdir()
        source=inputs/'executable.elf';output=root/'.local/output.ktprobe'
        expected=2
        if name=='existing':output.write_bytes(b'authored sentinel')
        elif name=='wrong-place':output=root/'user-data/output.ktprobe'
        elif name=='source-overlap':
            source=root/'.local/source/eboot.elf';source.parent.mkdir();source.write_bytes(fixtures.executable())
            output=source.with_suffix('.ktprobe')
        elif name=='junction':
            target=directory/'junction-target';target.mkdir()
            link=root/'.local/link'
            created=subprocess.run(['cmd','/c','mklink','/J',str(link),str(target)],capture_output=True)
            if created.returncode:raise RuntimeError('Cannot create authored junction fixture')
            output=link/'output.ktprobe'
        elif name=='alternate-stream':output=root/'.local/output.ktprobe:stream'
        elif name=='other-game-overlap':
            (root/'config.ini').write_text('[game.futari]\ndata_dir=.local/blocked\n',encoding='utf-8')
            output=root/'.local/blocked/output.ktprobe'
        elif name=='other-dlc-overlap':
            (root/'config.ini').write_text('[game.futari]\ndlc_dir=.local/blocked\n',encoding='utf-8')
            output=root/'.local/blocked/output.ktprobe'
        elif name=='invalid-sibling':
            (root/'config.ini').write_text('[game.futari]\ndata_dir=Q:relative\n',encoding='utf-8')
            expected=0
        elif name=='relative-component':output=root/'.local/../.local/output.ktprobe'
        before=digest(source);config=root/'config.ini';config_before=digest(config) if config.exists() else None
        previous=output.read_bytes() if output.exists() else None
        result=subprocess.run([str(executable),'--prepare-elf',str(source),'--root',str(root),'--output',str(output)],
                              cwd=directory,capture_output=True,timeout=20)
        passed=result.returncode==expected and digest(source)==before and (digest(config) if config.exists() else None)==config_before
        if expected==0:
            oracle,_=prepare_native.prepare(source,{})
            passed=passed and output.read_bytes()==oracle
        else:
            passed=passed and ((output.read_bytes()==previous) if previous is not None else not output.exists())
        if name=='junction':passed=passed and not any(target.iterdir())
        for suffix,content in (('stdout.jsonl',result.stdout),('stderr.txt',result.stderr)):
            with (directory/('path-'+name+'.'+suffix)).open('xb') as target_file:target_file.write(content)
        results.append(dict(check='path-'+name,passed=passed,exitCode=result.returncode,expectedExitCode=expected))
        print(('PASS' if passed else 'FAIL')+': prepare-path-'+name,flush=True)
    return results


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    receipt=verify()
    directory=private_output(args.output or ROOT/'.local/ps4-prepare-checks'/
        (datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ-')+uuid.uuid4().hex[:8]))
    inputs=directory/'原始 输入'; outputs=directory/'准备 输出'
    inputs.mkdir(parents=True);outputs.mkdir()
    executable=ROOT/'out/ps4-native/KetsuiNativeProbe.exe'
    results=[]
    for name,raw,module,contract,valid in fixtures.cases():
        source=inputs/(name+'.elf');source.write_bytes(raw)
        output=outputs/(name+('.ktmodule' if module else '.ktprobe'))
        command=[str(executable),'--prepare-elf',str(source),'--root',str(ROOT),'--output',str(output)]
        libc=inputs/(name+'.libc')
        if module: command.append('--module')
        if contract is not None:
            libc.write_bytes(contract);command.extend(('--libc',str(libc)))
        before=digest(source)
        result=subprocess.run(command,cwd=directory,capture_output=True,timeout=20)
        passed=(result.returncode==0)==valid and (output.exists()==valid) and digest(source)==before
        if contract is not None: passed=passed and libc.read_bytes()==contract
        if valid and passed:
            expected,report=prepare_native.prepare(source,{},libc if contract is not None else None,module)
            passed=output.read_bytes()==expected
            native=json.loads(result.stdout)
            passed=passed and native['sourceSha256']==before and native['preparedSha256']==digest(output) and native['guestExecution'] is False
            # A valid first output cannot be replaced by a second invocation.
            repeated=subprocess.run(command,cwd=directory,capture_output=True,timeout=20)
            passed=passed and repeated.returncode==2 and output.read_bytes()==expected
        for suffix,content in (('stdout.jsonl',result.stdout),('stderr.txt',result.stderr)):
            with (directory/(name+'.'+suffix)).open('xb') as target:target.write(content)
        results.append(dict(check=name,passed=passed,exitCode=result.returncode,expectedValid=valid))
        print(('PASS' if passed else 'FAIL')+': prepare-'+name,flush=True)
    results.extend(path_checks(executable,directory,inputs))
    write_json_new(directory/'result.json',dict(format=1,gameExecution=False,sourceCommit=receipt['sourceCommit'],
        sourceDirty=receipt['sourceDirty'],executableSha256=digest(executable),checks=results))
    print(f'{sum(item["passed"] for item in results)}/{len(results)} native preparation checks passed; report: {directory}')
    return 0 if all(item['passed'] for item in results) else 1


if __name__=='__main__':sys.exit(main())
