"""Bounded read-only Windows process CPU sampling using equivalent logical cores."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from project import digest, write_json_new


def thread_times(kernel, pid):
    """Read thread accounting without suspending or waking target threads."""
    class Entry(c.Structure):
        _fields_ = [(name,w.DWORD) for name in ('size','usage','id','owner','priority','delta','flags')]
    kernel.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD]
    kernel.CreateToolhelp32Snapshot.restype=w.HANDLE
    kernel.Thread32First.argtypes=kernel.Thread32Next.argtypes=[w.HANDLE,c.POINTER(Entry)]
    kernel.OpenThread.argtypes=[w.DWORD,w.BOOL,w.DWORD]
    kernel.OpenThread.restype=w.HANDLE
    kernel.GetThreadTimes.argtypes=[w.HANDLE,*([c.POINTER(w.FILETIME)]*4)]
    kernel.CloseHandle.argtypes=[w.HANDLE]
    snapshot=kernel.CreateToolhelp32Snapshot(4,0)
    if snapshot==c.c_void_p(-1).value:
        raise c.WinError(c.get_last_error())
    result={}
    def integer(value): return value.dwLowDateTime | (value.dwHighDateTime<<32)
    try:
        entry=Entry(); entry.size=c.sizeof(entry)
        valid=kernel.Thread32First(snapshot,c.byref(entry))
        while valid:
            if entry.owner==pid:
                handle=kernel.OpenThread(0x800,False,entry.id)
                if handle:
                    try:
                        stamps=[w.FILETIME() for _ in range(4)]
                        if kernel.GetThreadTimes(handle,*(c.byref(value) for value in stamps)):
                            qpc=c.c_longlong()
                            if not kernel.QueryPerformanceCounter(c.byref(qpc)):
                                raise c.WinError(c.get_last_error())
                            result[str(entry.id)]=dict(created=integer(stamps[0]),
                                qpc=qpc.value,kernel_100ns=integer(stamps[2]),user_100ns=integer(stamps[3]))
                    finally: kernel.CloseHandle(handle)
            valid=kernel.Thread32Next(snapshot,c.byref(entry))
        return result
    finally: kernel.CloseHandle(snapshot)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--seconds', type=int, choices=range(1, 61), default=20)
    parser.add_argument('--scene', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--threads', action='store_true', help='Add read-only per-thread CPU accounting at both window boundaries')
    args = parser.parse_args()
    if args.pid <= 0 or not args.scene.strip() or args.output.exists():
        raise ValueError('Explicit process, scene and fresh output required')
    kernel = c.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.GetProcessTimes.argtypes = [w.HANDLE, *([c.POINTER(w.FILETIME)] * 4)]
    kernel.QueryPerformanceCounter.argtypes = [c.POINTER(c.c_longlong)]
    kernel.QueryPerformanceFrequency.argtypes = [c.POINTER(c.c_longlong)]
    kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
    kernel.GetExitCodeProcess.argtypes = [w.HANDLE, c.POINTER(w.DWORD)]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    class Memory(c.Structure):
        _fields_ = [('cb',w.DWORD),('PageFaultCount',w.DWORD)] + [
            (name,c.c_size_t) for name in ('PeakWorkingSetSize','WorkingSetSize',
                'QuotaPeakPagedPoolUsage','QuotaPagedPoolUsage','QuotaPeakNonPagedPoolUsage',
                'QuotaNonPagedPoolUsage','PagefileUsage','PeakPagefileUsage','PrivateUsage')]
    class Io(c.Structure):
        _fields_ = [(name,c.c_ulonglong) for name in ('ReadOperationCount','WriteOperationCount',
            'OtherOperationCount','ReadTransferCount','WriteTransferCount','OtherTransferCount')]
    kernel.K32GetProcessMemoryInfo.argtypes = [w.HANDLE,c.POINTER(Memory),w.DWORD]
    kernel.GetProcessIoCounters.argtypes = [w.HANDLE,c.POINTER(Io)]
    handle = kernel.OpenProcess(0x1000, False, args.pid)  # QUERY_LIMITED_INFORMATION
    if not handle:
        raise c.WinError(c.get_last_error())
    def integer(filetime):
        return filetime.dwLowDateTime | (filetime.dwHighDateTime << 32)
    def sample():
        stamps = [w.FILETIME() for _ in range(4)]
        qpc, status = c.c_longlong(), w.DWORD()
        if (not kernel.GetProcessTimes(handle, *(c.byref(s) for s in stamps)) or
                not kernel.GetExitCodeProcess(handle, c.byref(status)) or status.value != 259 or
                not kernel.QueryPerformanceCounter(c.byref(qpc))):
            raise RuntimeError('Target exited or process timing is unavailable')
        row = dict(qpc=qpc.value, created=integer(stamps[0]),
                   cpu_100ns=integer(stamps[2]) + integer(stamps[3]))
        memory, io = Memory(), Io()
        memory.cb=c.sizeof(memory)
        if kernel.K32GetProcessMemoryInfo(handle,c.byref(memory),c.sizeof(memory)):
            row.update(page_faults=memory.PageFaultCount,working_set_bytes=memory.WorkingSetSize,
                       private_bytes=memory.PrivateUsage)
        if kernel.GetProcessIoCounters(handle,c.byref(io)):
            row.update(read_bytes=io.ReadTransferCount,write_bytes=io.WriteTransferCount,
                       read_operations=io.ReadOperationCount,other_operations=io.OtherOperationCount)
        return row
    try:
        name, length, frequency = c.create_unicode_buffer(32768), w.DWORD(32768), c.c_longlong()
        if not kernel.QueryFullProcessImageNameW(handle, 0, name, c.byref(length)) or not kernel.QueryPerformanceFrequency(c.byref(frequency)):
            raise c.WinError(c.get_last_error())
        executable = Path(name.value)
        identity = digest(executable)
        rows = [sample()]
        threads_begin=thread_times(kernel,args.pid) if args.threads else {}
        for _ in range(args.seconds):
            time.sleep(1)
            rows.append(sample())
        threads_end=thread_times(kernel,args.pid) if args.threads else {}
        if len({r['created'] for r in rows}) != 1 or digest(executable) != identity:
            raise ValueError('Process or executable identity changed')
        wall = (rows[-1]['qpc'] - rows[0]['qpc']) / frequency.value
        cpu = (rows[-1]['cpu_100ns'] - rows[0]['cpu_100ns']) / 10_000
        result = dict(pid=args.pid, executable=str(executable), executable_sha256=identity,
            scene=args.scene, qpc_frequency=frequency.value, samples=rows,
            wall_seconds=wall, cpu_ms=cpu, cpu_ms_per_second=cpu/wall,
            equivalent_logical_cores=cpu/wall/1000,
            sampler_sha256=digest(Path(__file__)))
        if args.threads:
            paired={}
            for tid,begin in threads_begin.items():
                end=threads_end.get(tid)
                if not end or begin['created']!=end['created']: continue
                kernel_ms=(end['kernel_100ns']-begin['kernel_100ns'])/10000
                user_ms=(end['user_100ns']-begin['user_100ns'])/10000
                thread_wall=(end['qpc']-begin['qpc'])/frequency.value
                if thread_wall<=0 or kernel_ms<0 or user_ms<0:
                    raise ValueError('Thread accounting moved backwards')
                paired[tid]=dict(created=begin['created'],kernel_ms=kernel_ms,user_ms=user_ms,
                                 cpu_ms=kernel_ms+user_ms,wall_seconds=thread_wall,
                                 cores=(kernel_ms+user_ms)/thread_wall/1000)
            result['thread_accounting']=dict(begin=threads_begin,end=threads_end,paired=paired,
                method='Read-only GetThreadTimes; threads are never suspended',
                limitations=['Boundary enumeration adds a small timestamp skew.',
                    'CPU accounting is quantized; zero is below accounting resolution.',
                    'Threads missing at either boundary are excluded from paired totals.'])
        write_json_new(args.output, result)
        print(json.dumps({k:v for k,v in result.items() if k not in ('samples','thread_accounting')}, indent=2))
        if args.threads:
            print(json.dumps(dict(top_threads=sorted(paired.items(),key=lambda row:row[1]['cpu_ms'],reverse=True)[:8]),indent=2))
    finally:
        kernel.CloseHandle(handle)


if __name__ == '__main__':
    main()
