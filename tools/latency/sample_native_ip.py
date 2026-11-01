"""Bounded instruction-pointer sampling of an explicitly named local test EXE.

Uses short SuspendThread/GetThreadContext/ResumeThread pairs. Symbol lookup runs
after sampling, with every target thread resumed. No administrator/ETW session.
"""
import argparse
import collections
import ctypes as c
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--expected-exe', type=Path, required=True)
    parser.add_argument('--symbols', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=8)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if os.name != 'nt' or not 1 <= args.seconds <= 60:
        raise SystemExit('Windows x64 and a 1..60 second duration are required')
    kernel = c.WinDLL('kernel32', use_last_error=True)
    dbg = c.WinDLL('dbghelp', use_last_error=True)
    def api(dll, name, result, *arguments):
        function = getattr(dll, name)
        function.restype, function.argtypes = result, arguments
        return function
    close = api(kernel,'CloseHandle',w.BOOL,w.HANDLE)
    process = api(kernel,'OpenProcess',w.HANDLE,w.DWORD,w.BOOL,w.DWORD)(0x410,False,args.pid)
    if not process: raise c.WinError(c.get_last_error())
    handles = {}
    try:
        path = c.create_unicode_buffer(32768)
        length = w.DWORD(len(path))
        query = api(kernel,'QueryFullProcessImageNameW',w.BOOL,w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD))
        if not query(process,0,path,c.byref(length)): raise c.WinError(c.get_last_error())
        if Path(path.value).resolve() != args.expected_exe.resolve():
            raise RuntimeError('Target EXE path differs from --expected-exe')
        class Thread(c.Structure):
            _fields_ = [(name,w.DWORD) for name in ('size','usage','id','owner','priority','delta','flags')]
        snapshot = api(kernel,'CreateToolhelp32Snapshot',w.HANDLE,w.DWORD,w.DWORD)(4,0)
        first = api(kernel,'Thread32First',w.BOOL,w.HANDLE,c.POINTER(Thread))
        next_thread = api(kernel,'Thread32Next',w.BOOL,w.HANDLE,c.POINTER(Thread))
        open_thread = api(kernel,'OpenThread',w.HANDLE,w.DWORD,w.BOOL,w.DWORD)
        entry = Thread(); entry.size = c.sizeof(entry)
        try:
            valid = first(snapshot,c.byref(entry))
            while valid:
                if entry.owner == args.pid:
                    handle = open_thread(0x4a,False,entry.id)
                    if handle: handles[entry.id] = handle
                valid = next_thread(snapshot,c.byref(entry))
        finally: close(snapshot)
        times = api(kernel,'GetThreadTimes',w.BOOL,w.HANDLE,*([c.POINTER(w.FILETIME)]*4))
        def cpu(handle):
            values = [w.FILETIME() for _ in range(4)]
            if not times(handle,*(c.byref(v) for v in values)): return 0
            return sum((v.dwHighDateTime<<32)|v.dwLowDateTime for v in values[2:])
        initial = {tid:cpu(h) for tid,h in handles.items()}
        time.sleep(.25)
        active = {tid:h for tid,h in handles.items() if cpu(h)-initial[tid] >= 20000}
        if not active: raise RuntimeError('No active target threads during calibration')
        suspend = api(kernel,'SuspendThread',w.DWORD,w.HANDLE)
        resume = api(kernel,'ResumeThread',w.DWORD,w.HANDLE)
        context = api(kernel,'GetThreadContext',w.BOOL,w.HANDLE,c.c_void_p)
        storage = c.create_string_buffer(1232+15)
        address = (c.addressof(storage)+15)&~15
        counts = collections.Counter()
        failed = 0
        started = time.perf_counter()
        deadline = started+args.seconds
        while time.perf_counter()<deadline:
            for tid,handle in active.items():
                c.c_uint32.from_address(address+48).value = 0x100001 # AMD64 CONTROL
                if suspend(handle) == 0xffffffff:
                    failed += 1
                    continue
                try:
                    if context(handle,address):
                        counts[(tid,c.c_uint64.from_address(address+248).value)] += 1
                    else: failed += 1
                finally:
                    if resume(handle) == 0xffffffff:
                        raise RuntimeError(f'Cannot resume target thread {tid}')
            time.sleep(.01)
        elapsed = time.perf_counter()-started
        final_cpu = {tid:(cpu(h)-initial[tid])/10000 for tid,h in handles.items()}
        options = api(dbg,'SymSetOptions',w.DWORD,w.DWORD)
        options(0x2|0x4|0x200) # undecorated, deferred, no UI
        initialize = api(dbg,'SymInitializeW',w.BOOL,w.HANDLE,w.LPCWSTR,w.BOOL)
        cleanup = api(dbg,'SymCleanup',w.BOOL,w.HANDLE)
        from_address = api(dbg,'SymFromAddrW',w.BOOL,w.HANDLE,c.c_uint64,c.POINTER(c.c_uint64),c.c_void_p)
        resolved = {}
        if initialize(process,str(args.symbols.resolve()),True):
            try:
                for _,ip in counts:
                    if ip in resolved: continue
                    symbol = c.create_string_buffer(88+2048)
                    c.c_uint32.from_buffer(symbol,0).value = 88
                    c.c_uint32.from_buffer(symbol,80).value = 1024
                    displacement = c.c_uint64()
                    if from_address(process,ip,c.byref(displacement),symbol):
                        resolved[ip] = c.wstring_at(c.addressof(symbol)+84)
                    else: resolved[ip] = f'0x{ip:x}'
            finally: cleanup(process)
        grouped = collections.Counter()
        for (tid,ip),count in counts.items(): grouped[(tid,resolved.get(ip,f'0x{ip:x}'))] += count
        result = {'pid':args.pid,'exe':path.value,'seconds':elapsed,'failed':failed,
                  'threads_cpu_ms':final_cpu,'samples':sum(counts.values()),
                  'functions':[{'tid':tid,'name':name,'samples':count}
                               for (tid,name),count in grouped.most_common()],
                  'ips':[{'tid':tid,'ip':f'0x{ip:x}','name':resolved.get(ip,''),'samples':count}
                         for (tid,ip),count in counts.most_common()]}
        args.output.write_text(json.dumps(result,indent=2),encoding='utf-8')
        print(json.dumps({k:result[k] for k in ('seconds','samples','failed','threads_cpu_ms')}))
        print(json.dumps(result['functions'][:18],indent=2))
    finally:
        for handle in handles.values(): close(handle)
        close(process)


if __name__ == '__main__': main()
