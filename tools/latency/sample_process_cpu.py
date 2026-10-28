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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--seconds', type=int, choices=range(1, 61), default=20)
    parser.add_argument('--scene', required=True)
    parser.add_argument('--output', type=Path, required=True)
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
        return dict(qpc=qpc.value, created=integer(stamps[0]),
                    cpu_100ns=integer(stamps[2]) + integer(stamps[3]))
    try:
        name, length, frequency = c.create_unicode_buffer(32768), w.DWORD(32768), c.c_longlong()
        if not kernel.QueryFullProcessImageNameW(handle, 0, name, c.byref(length)) or not kernel.QueryPerformanceFrequency(c.byref(frequency)):
            raise c.WinError(c.get_last_error())
        executable = Path(name.value)
        identity = digest(executable)
        rows = [sample()]
        for _ in range(args.seconds):
            time.sleep(1)
            rows.append(sample())
        if len({r['created'] for r in rows}) != 1 or digest(executable) != identity:
            raise ValueError('Process or executable identity changed')
        wall = (rows[-1]['qpc'] - rows[0]['qpc']) / frequency.value
        cpu = (rows[-1]['cpu_100ns'] - rows[0]['cpu_100ns']) / 10_000
        result = dict(pid=args.pid, executable=str(executable), executable_sha256=identity,
            scene=args.scene, qpc_frequency=frequency.value, samples=rows,
            wall_seconds=wall, cpu_ms=cpu, cpu_ms_per_second=cpu/wall,
            equivalent_logical_cores=cpu/wall/1000,
            sampler_sha256=digest(Path(__file__)))
        write_json_new(args.output, result)
        print(json.dumps({k:v for k,v in result.items() if k != 'samples'}, indent=2))
    finally:
        kernel.CloseHandle(handle)


if __name__ == '__main__':
    main()
