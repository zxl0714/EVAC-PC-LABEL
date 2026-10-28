"""Control only an explicitly enabled in-process synthetic test controller.

This does not send Windows input or measure the physical/controller front end.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import re
import time

BUTTONS = dict(up=1, down=2, left=4, right=8, start=16, back=32,
               a=0x1000, b=0x2000, x=0x4000, y=0x8000)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pipe', required=True)
    parser.add_argument('--id', required=True, type=int)
    parser.add_argument('--hold-ms', type=int, default=120)
    parser.add_argument('--buttons', required=True, help='Comma-separated names or 0x mask')
    parser.add_argument('--repeat', type=int, default=1, help='Bounded diagnostic pulse count')
    parser.add_argument('--alternate', help='Alternate pulse button name/mask')
    parser.add_argument('--gap-ms', type=int, default=97, help='Released interval between pulses')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,127}', args.pipe):
        parser.error('Invalid pipe name')
    if (not 0 < args.id < 2**64 - args.repeat or not 1 <= args.hold_ms <= 2000 or
            not 1 <= args.repeat <= 100 or not 10 <= args.gap_ms <= 2000):
        parser.error('Invalid command ID or hold duration')
    def button_mask(text):
        mask = int(text, 16) if text.startswith('0x') else sum(
            BUTTONS[name] for name in set(text.lower().split(',')))
        if not 0 <= mask <= 65535:
            parser.error('Invalid button mask')
        return mask
    masks = [button_mask(args.buttons), button_mask(args.alternate or args.buttons)]
    kernel = c.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p,
                                   w.DWORD, w.DWORD, w.HANDLE]
    kernel.CreateFileW.restype = w.HANDLE
    kernel.ReadFile.argtypes = [w.HANDLE, c.c_void_p, w.DWORD, c.POINTER(w.DWORD), c.c_void_p]
    kernel.WriteFile.argtypes = kernel.ReadFile.argtypes
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.WaitNamedPipeW.argtypes = [w.LPCWSTR, w.DWORD]
    path = '\\\\.\\pipe\\' + args.pipe
    for index in range(args.repeat):
        if index:
            time.sleep((args.hold_ms + args.gap_ms) / 1000)
        if not kernel.WaitNamedPipeW(path, 5000):
            raise c.WinError(c.get_last_error())
        send(kernel, path, args.id + index, args.hold_ms, masks[index % 2])


def send(kernel, path, command, hold_ms, mask):
    handle = kernel.CreateFileW(path, 0xC0000000, 0, None, 3, 0, None)
    if handle == c.c_void_p(-1).value:
        raise c.WinError(c.get_last_error())
    try:
        data = f'{command} {hold_ms} {mask:x}\n'.encode('ascii')
        written = w.DWORD()
        if not kernel.WriteFile(handle, data, len(data), c.byref(written), None) or written.value != len(data):
            raise c.WinError(c.get_last_error())
        response = c.create_string_buffer(128)
        count = w.DWORD()
        if not kernel.ReadFile(handle, response, 128, c.byref(count), None):
            raise c.WinError(c.get_last_error())
        result = response.raw[:count.value].decode('ascii').strip()
        if result != 'OK':
            raise RuntimeError(result)
        print(f'Synthetic guest input accepted: id={command}, mask=0x{mask:04x}, requested_hold_ms={hold_ms}', flush=True)
    finally:
        kernel.CloseHandle(handle)


if __name__ == '__main__':
    main()
