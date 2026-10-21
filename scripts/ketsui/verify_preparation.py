"""Compare native/Python preparation of explicitly supplied Ketsui data, without running it."""
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
from ps4_inventory import inventory,sfo
from generate_catalogue import load
sys.path.insert(0,str(ROOT/'scripts/ps4'))
from prepare_native import prepare
sys.path.insert(0,str(ROOT/'scripts/ketsui'))
from game_fixes import prepare_music_fix


def mount_arguments(data):
    arguments=['--app-dir',str(data)]
    if sfo(data/'sce_sys/param.sfo')['APP_VER']=='01.02':arguments+=['--merged-app','on']
    return arguments


def verify_cache(exe,data,directory):
    collection=directory/'缓存 portable collection'
    collection.mkdir()
    (collection/'config.ini').write_bytes((ROOT/'config/default.ini').read_bytes())
    command=[str(exe),'--prepare-game','--root',str(collection),'--data-dir',str(data)]
    responses=[]
    for phase in ('first','warm'):
        result=subprocess.run(command,cwd=directory,capture_output=True,timeout=120)
        for suffix,content in [('stdout.jsonl',result.stdout),('stderr.txt',result.stderr)]:
            with (directory/('cache-'+phase+'.'+suffix)).open('xb') as target:target.write(content)
        if result.returncode:raise RuntimeError('Native complete preparation failed: '+str(directory))
        response=json.loads(result.stdout)
        responses.append(response)
        folder=Path(response['directory'])
        snapshot={path.relative_to(folder).as_posix():digest(path) for path in folder.rglob('*') if path.is_file()}
        pointer=digest(collection/'prepared/ketsui/active.json')
        if phase=='first':before_snapshot,before_pointer=snapshot,pointer
        else:unchanged=snapshot==before_snapshot and pointer==before_pointer
    first,warm=responses
    outputs_match=all(digest(Path(first['directory'])/cache_file)==digest(directory/oracle_file) for cache_file,oracle_file in (
        ('eboot.ktprobe','eboot.bin.ktprobe'),('modules/emu_integration_01400.prx.ktmodule','emu_integration_01400.prx.ktmodule'),
        ('modules/ketsui_01000.prx.ktmodule','ketsui_01000.prx.ktmodule'),('game-fixes.ktfix','offline.ktfix')))
    _,metadata=prepare_music_fix(data,community_offline=True)
    outputs_match=outputs_match and json.loads((Path(first['directory'])/'game-fixes.json').read_text())==metadata
    validation=subprocess.run([str(exe),first['executable'],'--validate','--module-dir',first['modules'],
        *mount_arguments(data),
        '--read-patches',first['readFixes']],cwd=directory,capture_output=True,timeout=30)
    for suffix,content in [('stdout.jsonl',validation.stdout),('stderr.txt',validation.stderr)]:
        with (directory/('cache-validation.'+suffix)).open('xb') as target:target.write(content)
    events=[json.loads(line) for line in validation.stdout.splitlines() if line.startswith(b'{')]
    validated=validation.returncode==0 and any(item.get('event')=='validation_complete' and item.get('game_entry_reached') is False for item in events)
    passed=(first['cacheHit'] is False and warm['cacheHit'] is True and first['directory']==warm['directory'] and unchanged and outputs_match and validated
        and all(item['sourceValidated'] and item['preparedOutputsVerified'] and item['guestExecution'] is False for item in responses)
        and not (collection/'user-data').exists() and not (collection/'logs').exists())
    print(('PASS' if passed else 'FAIL')+': native complete preparation and warm cache without Python dependency',flush=True)
    return dict(passed=passed,firstCacheHit=first['cacheHit'],warmCacheHit=warm['cacheHit'],warmFilesUnchanged=unchanged,
        outputsMatchOracle=outputs_match,contentValidated=validated,directory=first['directory'],guestExecution=False)


def verify_read_fixes(exe,data,directory,probe):
    checks=[]
    for name,replay,offline in [('music',False,False),('replay',True,False),('offline',False,True)]:
        output=directory/(name+'.ktfix')
        command=[str(exe),'--prepare-fixes',str(data),'--root',str(ROOT),'--output',str(output)]
        if replay:command.append('--community-replay')
        if offline:command.append('--community-offline')
        result=subprocess.run(command,cwd=directory,capture_output=True,timeout=60)
        for suffix,content in [('stdout.jsonl',result.stdout),('stderr.txt',result.stderr)]:
            with (directory/('read-fixes-'+name+'.'+suffix)).open('xb') as target:target.write(content)
        if result.returncode:raise RuntimeError('Native resource preparation failed: '+str(directory))
        expected,metadata=prepare_music_fix(data,community_replay=replay,community_offline=offline)
        native=json.loads(result.stdout)
        # The pinned compressor currently produces identical bytes as well as
        # identical scripts. Do not weaken this check silently if it changes.
        matched=output.read_bytes()==expected and all(native.get(key)==value for key,value in metadata.items())
        validation=subprocess.run([str(exe),str(probe),'--validate','--module-dir',str(directory),
            *mount_arguments(data),
            '--read-patches',str(output)],cwd=directory,capture_output=True,timeout=30)
        for suffix,content in [('stdout.jsonl',validation.stdout),('stderr.txt',validation.stderr)]:
            with (directory/('content-validation-'+name+'.'+suffix)).open('xb') as target:target.write(content)
        events=[json.loads(line) for line in validation.stdout.splitlines() if line.startswith(b'{')]
        validated=validation.returncode==0 and any(item.get('event')=='validation_complete' and item.get('game_entry_reached') is False for item in events)
        before=digest(output)
        repeated=subprocess.run(command,cwd=directory,capture_output=True,timeout=60)
        unchanged=repeated.returncode==2 and digest(output)==before
        checks.append(dict(fixes=name,bytes=output.stat().st_size,sha256=before,byteIdentical=matched,
            contentValidated=validated,existingOutputPreserved=unchanged,guestExecution=False))
        print(('PASS' if matched and validated and unchanged else 'FAIL')+': native read fixes '+name,flush=True)
    return checks


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-data',type=Path,required=True)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    receipt=verify()
    data=args.game_data.resolve()
    original_inventory=inventory(data)
    game=next(game for game in load(ROOT/'games/catalogue.json') if game['id']=='ketsui')
    variant=next((variant for variant in game['variants'] if variant['id']==original_inventory['variantId']),None)
    if not variant or original_inventory['files']!=variant['files']:
        raise ValueError('Game data differs from the supported complete inventory')
    directory=private_output(args.output or ROOT/'.local/ketsui-preparation'/
        (datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ-')+uuid.uuid4().hex[:8]),data)
    directory.mkdir(parents=True,exist_ok=False)
    exe=ROOT/'out/ps4-native/KetsuiNativeProbe.exe'
    libc=data/'sce_module/libc.prx'
    before_libc=digest(libc)
    results=[]
    for name,module in [('eboot.bin',False),('emu_integration_01400.prx',True),('ketsui_01000.prx',True)]:
        source=data/name
        output=directory/(name+('.ktmodule' if module else '.ktprobe'))
        before=digest(source)
        command=[str(exe),'--prepare-elf',str(source),'--root',str(ROOT),'--output',str(output),'--libc',str(libc)]
        if module:command.append('--module')
        result=subprocess.run(command,cwd=directory,capture_output=True,timeout=120)
        for suffix,content in (('stdout.jsonl',result.stdout),('stderr.txt',result.stderr)):
            with (directory/(name+'.'+suffix)).open('xb') as target:target.write(content)
        if result.returncode!=0:raise RuntimeError('Native preparation failed; see '+str(directory))
        oracle,metadata=prepare(source,{},libc,module)
        matched=output.read_bytes()==oracle and digest(source)==before and digest(libc)==before_libc
        results.append(dict(program=name,sourceSha256=before,preparedSha256=digest(output),
            bytes=output.stat().st_size,imports=len(metadata['imports']),relocations=metadata['relocation_count'],
            byteIdentical=matched,sourceUnchanged=digest(source)==before))
        print(('PASS' if matched else 'FAIL')+': '+name,flush=True)
    validation=subprocess.run([str(exe),str(directory/'eboot.bin.ktprobe'),'--validate','--module-dir',str(directory)],
                              cwd=directory,capture_output=True,timeout=30)
    for suffix,content in (('stdout.jsonl',validation.stdout),('stderr.txt',validation.stderr)):
        with (directory/('validation.'+suffix)).open('xb') as target:target.write(content)
    events=[json.loads(line) for line in validation.stdout.splitlines() if line.startswith(b'{')]
    validated=validation.returncode==0 and any(event.get('event')=='validation_complete' and event.get('game_entry_reached') is False for event in events)
    print(('PASS' if validated else 'FAIL')+': native image validation without game entry',flush=True)
    read_fixes=verify_read_fixes(exe,data,directory,directory/'eboot.bin.ktprobe')
    cache=verify_cache(exe,data,directory)
    inputs_unchanged=inventory(data)==original_inventory
    print(('PASS' if inputs_unchanged else 'FAIL')+': complete read-only source inventory unchanged',flush=True)
    write_json_new(directory/'result.json',dict(format=1,game='ketsui',gameExecution=False,nativeImageValidation=validated,readFixes=read_fixes,
        inputsUnchanged=inputs_unchanged,dataFileCount=len(original_inventory['files']),
        preparedCache=cache,
        sourceCommit=receipt['sourceCommit'],sourceDirty=receipt['sourceDirty'],libcSha256=before_libc,
        executableSha256=digest(exe),checks=results))
    print('Prepared images contain local derived game data; report: '+str(directory))
    return 0 if cache['passed'] and inputs_unchanged and validated and all(item['byteIdentical'] for item in results) and all(
        item['byteIdentical'] and item['contentValidated'] and item['existingOutputPreserved'] for item in read_fixes) else 1


if __name__=='__main__':sys.exit(main())
