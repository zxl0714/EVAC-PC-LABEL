"""ROM-free Windows HTTP checks against independent authored PS4 services."""
import argparse
from contextlib import closing
import datetime
import hashlib
import json
from pathlib import Path
import sqlite3
import struct
import subprocess
import sys
import threading
import uuid
import zlib

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from analysis_paths import private_output
from project import ROOT, digest, write_json_new
from ps4_build_receipt import verify
sys.path.insert(0,str(ROOT))
from server.games import daioujou, ketsui
from replay_fixture import record


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
    profiles=[(game,profile,kind) for game,profile in [('ketsui',ketsui.Store),('daioujou',daioujou.Store)]
              for kind in ('client','native-score-replay')]
    for game,profile,kind in profiles:
        scene=directory/(game if kind=='client' else game+'-'+kind)
        scene.mkdir()
        store=profile(scene/'authored.sqlite3')
        server=ketsui.Server(0,store)
        thread=threading.Thread(target=server.serve_forever,kwargs=dict(poll_interval=0.01))
        thread.start()
        try:
            configuration=scene/'community.ini'
            with configuration.open('x',encoding='utf-8') as stream:
                stream.write('[community]\nenabled=true\nnickname=Ａlice\nendpoint=http://127.0.0.1:'+str(server.server_port)+store.api+'\n')
            if kind=='client':
                command=['--community-profile-smoke',str(configuration),str(scene/'client'),game]
            else:
                authored=record(game)
                struct.pack_into('<I',authored,108,9001)
                payload=zlib.compress(authored)
                attachment=scene/'authored.attachment'
                with attachment.open('xb') as stream:
                    stream.write(payload)
                command=['--np-replay-profile-smoke',str(configuration),str(scene/'client'),str(attachment),game]
            result=subprocess.run([str(executable),*command],
                cwd=scene,capture_output=True,timeout=30,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            for name,content in [('stdout.jsonl',result.stdout),('stderr.txt',result.stderr)]:
                with (scene/name).open('xb') as stream:
                    stream.write(content)
            events=[json.loads(line) for line in result.stdout.splitlines() if line.startswith(b'{')]
            final=events[-1] if events else {}
            passed=result.returncode==0 and final.get('passed') is True and final.get('game')==game
            board=0 if game=='ketsui' else 6
            page=store.leaderboard(board,'standard')
            expected=('9007199254740993' if game=='ketsui' else '123456789') if kind=='client' else '9002'
            passed=passed and page['total']==1 and page['entries'][0]['score']==expected
            if kind=='native-score-replay':
                # A newer score intentionally replaces the top row after the
                # old replay transfer starts. Its mismatched upload is refused;
                # only the original entry may retain the tested attachment.
                with closing(sqlite3.connect(store.database)) as database:
                    uploaded=database.execute('SELECT s.score,r.payload,r.sha256 FROM replays r JOIN scores s ON s.id=r.entry_id').fetchall()
                    scored=database.execute('SELECT score,accepted FROM scores ORDER BY sequence').fetchall()
                # Rejected/equal submissions retain their own immutable receipt
                # row; two accepted bests do not imply only two stored requests.
                passed=passed and scored==[(9001,1),(9002,1),(9002,0)] and page['entries'][0]['replay'] is None
                passed=passed and uploaded==[(9001,payload,hashlib.sha256(payload).hexdigest())]
                passed=passed and final.get('received_sha256')==hashlib.sha256(payload).hexdigest()
                passed=passed and final.get('native_game_execution') is False and final.get('native_playback_validated') is False
            results.append(dict(game=game,kind=kind,passed=passed,exitCode=result.returncode,
                nativeGameExecution=False,nativeReplayValidated=False))
            print(('PASS' if passed else 'FAIL')+': '+game+' '+kind,flush=True)
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
