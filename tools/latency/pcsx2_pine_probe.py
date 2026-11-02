"""Read-only local PCSX2 snapshots for ESPGaluda frame-step response review.

Protocol reference: PCSX2 v2.8.2 pcsx2/PINE.cpp. This client never sends writes,
savestate commands or input. It is not a wall-clock latency benchmark.
"""
import argparse
import json
from pathlib import Path
import socket
import struct
import time


def exact(connection, size):
    data=bytearray()
    while len(data)<size:
        part=connection.recv(size-len(data))
        if not part:raise RuntimeError('PINE closed an incomplete reply')
        data.extend(part)
    return bytes(data)


def request(connection, commands):
    connection.sendall(struct.pack('<I',len(commands)+4)+commands)
    length,=struct.unpack('<I',exact(connection,4))
    if not 5<=length<=450000:raise RuntimeError('Invalid PINE reply extent')
    reply=exact(connection,length-4)
    if reply[0]!=0:raise RuntimeError('PINE rejected read-only snapshot')
    return reply[1:]


def string(connection, opcode):
    value=request(connection,bytes([opcode]))
    if len(value)<4:raise RuntimeError('Truncated PINE string')
    size,=struct.unpack_from('<I',value)
    if size!=len(value)-4:raise RuntimeError('Invalid PINE string extent')
    return value[4:].rstrip(b'\0').decode('utf8')


def snapshot(port):
    addresses=[0x3589c8,0x3589d0,0x362900,0x3efac0,0x3efad8]
    addresses.extend(range(0x3effc0,0x3effdc,4))
    with socket.create_connection(('127.0.0.1',port),timeout=3) as connection:
        connection.settimeout(3)
        version=string(connection,8);serial=string(connection,0xc);crc=string(connection,0xd)
        if not version.startswith('PCSX2 ') or serial!='SLPS-25352':
            raise RuntimeError('Snapshot requires a running ESPGaluda SLPS-25352 VM')
        status=struct.unpack('<I',request(connection,b'\x0f'))[0]
        if status!=1:raise RuntimeError('Pause the emulator before a frame-step snapshot')
        commands=b''.join(b'\x02'+struct.pack('<I',address) for address in addresses)
        qpc_start=time.perf_counter_ns();result=request(connection,commands);qpc_end=time.perf_counter_ns()
    if len(result)!=4*len(addresses):raise RuntimeError('PINE memory reply size differs')
    values=struct.unpack('<'+'I'*len(addresses),result)
    return dict(version=version,serial=serial,crc=crc,status='paused',
        query_start_qpc_ns=qpc_start,query_end_qpc_ns=qpc_end,
        words={f'{address:08x}':value for address,value in zip(addresses,values)},
        scope='Read-only paused EE state, not physical or wall-clock latency')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',type=int,default=28011)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if not 1024<=args.port<=65535:parser.error('Use a local non-privileged PINE port')
    record=snapshot(args.port)
    with args.output.open('x',encoding='utf8') as target:json.dump(record,target,indent=2);target.write('\n')
    print(json.dumps(record))


if __name__=='__main__':main()
