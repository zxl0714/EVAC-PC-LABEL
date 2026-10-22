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
    profiles.append(('daioujou',daioujou.Store,'native-challenge'))
    profiles.extend(('ketsui',ketsui.Store,'native-custom-'+str(board)) for board in (60,61,62))
    profiles.append(('ketsui',ketsui.Store,'custom-round'))
    profiles.append(('ketsui',ketsui.Store,'native-base-replay'))
    for game,profile,kind in profiles:
        custom_board=int(kind.rsplit('-',1)[1]) if kind.startswith('native-custom-') else None
        replay_case=kind in ('native-score-replay','native-base-replay') or custom_board is not None
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
            elif kind=='native-challenge':
                command=['--np-challenge-smoke',str(configuration),str(scene/'client')]
            elif kind=='custom-round':
                command=['--community-round-client-smoke',str(configuration),str(scene/'client'),'custom']
            else:
                authored=record(game)
                struct.pack_into('<I',authored,108,9001)
                if kind=='native-base-replay':
                    struct.pack_into('<I',authored,4,0x10000)
                if custom_board is not None:
                    struct.pack_into('<H',authored,106,3)
                    struct.pack_into('<H',authored,112,4 if custom_board==62 else 0)
                payload=zlib.compress(authored)
                attachment=scene/'authored.attachment'
                with attachment.open('xb') as stream:
                    stream.write(payload)
                command=['--np-replay-profile-smoke',str(configuration),str(scene/'client'),str(attachment),game]
                if custom_board is not None:
                    command=['--np-custom-replay-smoke',str(configuration),str(scene/'client'),str(attachment),str(custom_board)]
            result=subprocess.run([str(executable),*command],
                cwd=scene,capture_output=True,timeout=30,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            for name,content in [('stdout.jsonl',result.stdout),('stderr.txt',result.stderr)]:
                with (scene/name).open('xb') as stream:
                    stream.write(content)
            events=[json.loads(line) for line in result.stdout.splitlines() if line.startswith(b'{')]
            final=events[-1] if events else {}
            passed=result.returncode==0 and final.get('passed') is True and (kind=='custom-round' or final.get('game')==game)
            board=custom_board if custom_board is not None else 60 if kind=='custom-round' else 99 if kind=='native-challenge' else 0 if game=='ketsui' else 6
            page=store.leaderboard(board,'standard')
            expected='9001' if kind=='custom-round' else '10003' if kind=='native-challenge' else ('9007199254740993' if game=='ketsui' else '123456789') if kind=='client' else '9002'
            passed=passed and page['total']==1 and page['entries'][0]['score']==expected
            if kind=='native-challenge':
                with closing(sqlite3.connect(store.database)) as database:
                    scored=database.execute('SELECT score,accepted FROM scores ORDER BY sequence').fetchall()
                    attachments=database.execute('SELECT count(*) FROM replays').fetchone()[0]
                passed=passed and scored==[(0,1),(10003,1),(10003,0),(3,0)] and attachments==0
                passed=passed and page['entries'][0]['replay'] is None and store.leaderboard(0,'standard')['total']==0
            if replay_case:
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
            if kind=='custom-round':
                with closing(sqlite3.connect(store.database)) as database:
                    rows=database.execute('SELECT s.board,s.score,r.payload FROM scores s JOIN replays r ON s.id=r.entry_id ORDER BY s.board').fetchall()
                passed=passed and [row[:2] for row in rows]==[(60,9001),(61,9001)] and rows[0][2]==rows[1][2]
                if passed:
                    raw=zlib.decompress(rows[0][2])
                    passed=struct.unpack_from('<H',raw,106)[0]==3 and struct.unpack_from('<I',raw,108)[0]==9001
                passed=passed and store.leaderboard(0,'standard')['total']==0 and final.get('pending_rounds')==0
                passed=passed and final.get('native_game_integration') is False
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
