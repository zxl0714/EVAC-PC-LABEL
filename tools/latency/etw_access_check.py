"""Read ETW provider access descriptors and check this process token.

This never changes an ACL, privilege, group, service, or trace session. A granted
provider ACL is not proof that its runtime events are delivered successfully.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'scripts'))
from project import write_json_new

PROVIDERS = {
    'DXGI': 'ca11c036-0102-4a2d-a6ad-f03cfed5d3c9',
    'DxgKrnl': '802ec45a-1e99-4b83-9920-87c98277ba9d',
    'Win32k': '8c416c79-d49b-4f01-a467-e56d3aa8234c',
    'DwmCore': '9e9bba3c-2e38-40cb-99f4-9e8281425164',
}
RIGHTS = {'query': 0x1, 'enable_provider': 0x80, 'write_event': 0x200,
          'consume_realtime': 0x400}


class Mapping(c.Structure):
    _fields_ = [(key, w.DWORD) for key in ('read', 'write', 'execute', 'all')]


def inspect():
    adv = c.WinDLL('advapi32', use_last_error=True)
    kernel = c.WinDLL('kernel32', use_last_error=True)
    kernel.GetCurrentProcess.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.LocalFree.argtypes = [c.c_void_p]
    adv.OpenProcessToken.argtypes = [w.HANDLE, w.DWORD, c.POINTER(w.HANDLE)]
    adv.DuplicateToken.argtypes = [w.HANDLE, c.c_int, c.POINTER(w.HANDLE)]
    adv.GetTokenInformation.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD, c.POINTER(w.DWORD)]
    adv.EventAccessQuery.argtypes = [c.c_void_p, c.c_void_p, c.POINTER(w.ULONG)]
    adv.EventAccessQuery.restype = w.ULONG
    adv.ConvertSecurityDescriptorToStringSecurityDescriptorW.argtypes = [
        c.c_void_p, w.DWORD, w.DWORD, c.POINTER(w.LPWSTR), c.POINTER(w.ULONG)]
    adv.AccessCheck.argtypes = [c.c_void_p, w.HANDLE, w.DWORD, c.POINTER(Mapping),
        c.c_void_p, c.POINTER(w.DWORD), c.POINTER(w.DWORD), c.POINTER(w.BOOL)]
    token, duplicate = w.HANDLE(), w.HANDLE()
    if not adv.OpenProcessToken(kernel.GetCurrentProcess(), 0xA, c.byref(token)):
        raise c.WinError(c.get_last_error())
    try:
        if not adv.DuplicateToken(token, 2, c.byref(duplicate)):
            raise c.WinError(c.get_last_error())
        elevation, returned = w.DWORD(), w.DWORD()
        if not adv.GetTokenInformation(token, 20, c.byref(elevation), c.sizeof(elevation), c.byref(returned)):
            raise c.WinError(c.get_last_error())
        result = dict(elevated=bool(elevation.value), providers={},
            scope='Read-only ETW provider ACL checks, not event delivery or display latency')
        for name, value in PROVIDERS.items():
            guid = c.create_string_buffer(uuid.UUID(value).bytes_le, 16)
            size = w.ULONG()
            status = adv.EventAccessQuery(guid, None, c.byref(size))
            if status not in (0, 234) or not size.value:
                result['providers'][name] = dict(query_status=status)
                continue
            descriptor = c.create_string_buffer(size.value)
            status = adv.EventAccessQuery(guid, descriptor, c.byref(size))
            if status:
                result['providers'][name] = dict(query_status=status)
                continue
            sddl = w.LPWSTR()
            if not adv.ConvertSecurityDescriptorToStringSecurityDescriptorW(
                    descriptor, 1, 4, c.byref(sddl), None):
                raise c.WinError(c.get_last_error())
            try:
                item = dict(query_status=0, sddl=sddl.value, rights={})
            finally:
                kernel.LocalFree(c.cast(sddl, c.c_void_p))
            # Specific access masks above contain no GENERIC_* bits. The mapping
            # is supplied for API completeness; no token privilege is enabled.
            mapping = Mapping(0x2000D, 0x20012, 0x20000, 0x121FFF)
            for right, mask in RIGHTS.items():
                privileges = c.create_string_buffer(4096)
                length, granted, allowed = w.DWORD(len(privileges)), w.DWORD(), w.BOOL()
                if not adv.AccessCheck(descriptor, duplicate, mask, c.byref(mapping),
                        privileges, c.byref(length), c.byref(granted), c.byref(allowed)):
                    raise c.WinError(c.get_last_error())
                item['rights'][right] = dict(allowed=bool(allowed.value), granted_mask=granted.value)
            result['providers'][name] = item
        return result
    finally:
        if duplicate.value: kernel.CloseHandle(duplicate)
        kernel.CloseHandle(token)


def main():
    import json
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = inspect()
    write_json_new(args.output, result)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
