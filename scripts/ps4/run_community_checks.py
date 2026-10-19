"""ROM-free Windows HTTP checks against independent authored PS4 services."""
import argparse
import datetime
import json
from pathlib import Path
import subprocess
import sys
import threading
import uuid

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from analysis_paths import private_output
from project import ROOT, digest, write_json_new
from ps4_build_receipt import verify
sys.path.insert(0,str(ROOT))
from server.games import daioujou, ketsui


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    receipt=verify()
    directory=private_output(args.output or ROOT/'.local/ps4-community-checks'/
        (datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ-')+uuid.uuid4().hex[:8]))
    directory.mkdir(parents=True,exist_ok=False)
    executable=ROOT/'out/ps4-native/KetsuiNativeProbe.exe'
    results=[]
    for game,profile in [('ketsui',ketsui.Store),('daioujou',daioujou.Store)]:
        scene=directory/game
        scene.mkdir()
        store=profile(scene/'authored.sqlite3')
        server=ketsui.Server(0,store)
        thread=threading.Thread(target=server.serve_forever,kwargs=dict(poll_interval=0.01))
        thread.start()
        try:
            configuration=scene/'community.ini'
            with configuration.open('x',encoding='utf-8') as stream:
                stream.write('[community]\nenabled=true\nnickname=Ａlice\nendpoint=http://127.0.0.1:'+str(server.server_port)+store.api+'\n')
            result=subprocess.run([str(executable),'--community-profile-smoke',str(configuration),str(scene/'client'),game],
                cwd=scene,capture_output=True,timeout=30,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            for name,content in [('stdout.jsonl',result.stdout),('stderr.txt',result.stderr)]:
                with (scene/name).open('xb') as stream:
                    stream.write(content)
            events=[json.loads(line) for line in result.stdout.splitlines() if line.startswith(b'{')]
            final=events[-1] if events else {}
            passed=result.returncode==0 and final.get('passed') is True and final.get('game')==game
            board=0 if game=='ketsui' else 6
            page=store.leaderboard(board,'standard')
            expected='9007199254740993' if game=='ketsui' else '123456789'
            passed=passed and page['total']==1 and page['entries'][0]['score']==expected
            results.append(dict(game=game,passed=passed,exitCode=result.returncode,
                nativeGameExecution=False,nativeReplayValidated=False))
            print(('PASS' if passed else 'FAIL')+': '+game,flush=True)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(5)
            if thread.is_alive():
                raise RuntimeError('Authored HTTP service did not stop')
    write_json_new(directory/'result.json',dict(format=1,buildSourceCommit=receipt['sourceCommit'],
        buildSourceDirty=receipt['sourceDirty'],executableSha256=digest(executable),
        authoredDataOnly=True,externalServiceAccess=False,checks=results))
    return 0 if all(item['passed'] for item in results) else 1


if __name__=='__main__':
    sys.exit(main())
