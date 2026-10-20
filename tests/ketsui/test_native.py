"""Native-probe tests use only synthetic machine code authored for this project."""

import json
import datetime
import math
import hashlib
import importlib.util
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time as host_time
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from native_elf import Elf, nid, encode_id

DEVICE_MODE = os.environ.get('KETSUI_TEST_DEVICE_MODE', 'all')
if DEVICE_MODE not in ('all', 'none'):
    raise ValueError('KETSUI_TEST_DEVICE_MODE must be all or none')
requires_audio_device = unittest.skipIf(DEVICE_MODE == 'none', 'Requires a real Windows audio endpoint')
requires_vulkan_device = unittest.skipIf(DEVICE_MODE == 'none', 'Requires a Vulkan device and presentation')


def sfo_document(values):
    pairs = list(values.items()) if isinstance(values, dict) else list(values)
    names, data, entries = bytearray(), bytearray(), bytearray()
    for name, value in pairs:
        key = len(names)
        names += name.encode('ascii') + b'\0'
        if isinstance(value, int):
            kind, raw = 0x404, struct.pack('<i', value)
        elif isinstance(value, str):
            kind, raw = 0x204, value.encode('utf-8') + b'\0'
        else:
            kind, raw = 4, bytes(value)
        capacity = (len(raw) + 3) // 4 * 4
        entries += struct.pack('<HHIII', key, kind, len(raw), capacity, len(data))
        data += raw + bytes(capacity - len(raw))
    keys_offset = 20 + len(entries)
    data_offset = (keys_offset + len(names) + 3) // 4 * 4
    return bytearray(struct.pack('<5I', 0x46535000, 0x101, keys_offset, data_offset, len(pairs)) + entries + names +
                     bytes(data_offset - keys_offset - len(names)) + data)


def content_tree(root, *, app_type=1, title_id='CUSA00001', content_id='XX0000-CUSA00001_00-BASEGAME00000000'):
    root = Path(root)
    app, patch, addons = root / '本体 files', root / '更新 files', root / '附加 内容'
    fields = dict(TITLE_ID=title_id, CONTENT_ID=content_id, CATEGORY='gd',
                  APP_TYPE=app_type, APP_VER='01.00', USER_DEFINED_PARAM_1=11, USER_DEFINED_PARAM_2=-17)
    for directory, values in ((app, fields), (patch, dict(TITLE_ID=fields['TITLE_ID'], CONTENT_ID=fields['CONTENT_ID'],
                                                       CATEGORY='gp', APP_TYPE=app_type, APP_VER='01.02', USER_DEFINED_PARAM_1=1234))):
        (directory / 'sce_sys').mkdir(parents=True)
        (directory / 'sce_sys/param.sfo').write_bytes(sfo_document(values))
    (app / 'overlay.txt').write_bytes(b'base')
    (patch / 'overlay.txt').write_bytes(b'patch')
    (app / 'fallback.txt').write_bytes(b'fallback')
    for index in (1, 2):
        directory = addons / str(index)
        (directory / 'sce_sys').mkdir(parents=True)
        (directory / 'sce_sys/param.sfo').write_bytes(sfo_document(dict(TITLE_ID=fields['TITLE_ID'],
              CONTENT_ID=f'{content_id[:20]}A00000000000000{index}', CATEGORY='ac')))
        (directory / 'payload.txt').write_bytes(b'addon')
    (addons / '1/entitlement.key').write_bytes(bytes(range(1, 17)))
    return app, patch, addons


def trophy_archive(xml=None, *, encrypted=False):
    if xml is None:
        xml = b'<trophyconf version="1.1"><trophy id="000" ttype="P" pid="-1"/><trophy id="001" ttype="B" pid="0"/><trophy id="002" ttype="G" pid="0"/></trophyconf>'
    name = b'TROPCONF.ESFM' if encrypted else b'TROPCONF.XML'
    size = 160 + len(xml)
    header = struct.pack('>IIQIII', 0xdca24d00, 3, size, 1, 64, 0) + bytes(68)
    entry = name.ljust(32, b'\0') + struct.pack('>QQI', 160, len(xml), 3 if encrypted else 0) + bytes(12)
    return header + entry + xml


def file_fixture(*, action='normal', path='/app0/overlay.txt', flags=0, expected=0,
                 library='libkernel', module='libkernel', read_expected=b'patch'):
    assert len(read_expected) == 5
    names = ['sceKernelCheckReachability', 'sceKernelOpen', 'sceKernelRead', 'sceKernelPread',
             'sceKernelLseek', 'sceKernelStat', 'sceKernelFstat', 'sceKernelClose', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(buffer=4400, stat=4500, sentinel=4620)
    asm.emit('48 83 ec 08')

    def call(name, value=0, *, wide=False):
        asm.relative('ff 15', name)
        asm.emit(('48 3d ' if wide else '3d ') + struct.pack('<I', value & 0xffffffff).hex(' '))
        asm.relative('0f 85', 'fail')

    asm.relative('48 8d 3d', 'path')
    if action == 'reach':
        call(names[0], expected)
    elif action == 'open':
        asm.emit('be ' + struct.pack('<I', flags).hex(' ') + ' 31 d2')
        call(names[1], expected)
        if expected >= 3 and expected < 1024:
            asm.emit('89 c7')
            call(names[7])
    else:
        call(names[0])
        asm.relative('48 8d 3d', 'path')
        asm.emit('31 f6 31 d2')
        call(names[1], 3)
        asm.emit('41 89 c4 44 89 e7')
        asm.relative('48 8d 35', 'buffer')
        asm.emit('ba 04 00 00 00')
        call(names[2], 4, wide=True)
        asm.relative('81 3d', 'buffer', read_expected[:4].hex(' '))
        asm.relative('0f 85', 'fail')
        asm.emit('44 89 e7')
        asm.relative('48 8d 35', 'buffer')
        asm.emit('ba 04 00 00 00 b9 01 00 00 00')
        call(names[3], 4, wide=True)
        asm.relative('81 3d', 'buffer', read_expected[1:].hex(' '))
        asm.relative('0f 85', 'fail')
        asm.emit('44 89 e7 31 f6 ba 01 00 00 00')
        call(names[4], 4, wide=True)  # pread left the sequential offset at four
        asm.relative('48 c7 05', 'sentinel', '49 00 00 00')
        asm.emit('44 89 e7')
        asm.relative('48 8d 35', 'stat')
        call(names[6])
        asm.relative('48 8d 05', 'stat')
        asm.emit('48 83 78 48 05')
        asm.relative('0f 85', 'fail')
        asm.relative('48 8d 3d', 'fallback')
        asm.relative('48 8d 35', 'stat')
        call(names[5])
        asm.relative('48 8d 05', 'stat')
        asm.emit('48 83 78 48 08')
        asm.relative('0f 85', 'fail')
        asm.relative('48 83 3d', 'sentinel', '49')
        asm.relative('0f 85', 'fail')
        asm.emit('44 89 e7 48 c7 c6 ff ff ff ff ba 02 00 00 00')
        call(names[4], 4, wide=True)
        asm.emit('44 89 e7')
        asm.relative('48 8d 35', 'buffer')
        asm.emit('ba 08 00 00 00')
        call(names[2], 1, wide=True)
        asm.relative('80 3d', 'buffer', read_expected[-1:].hex(' '))
        asm.relative('0f 85', 'fail')
        asm.emit('44 89 e7')
        call(names[7])
        asm.emit('44 89 e7')
        asm.relative('48 8d 35', 'buffer')
        asm.emit('ba 01 00 00 00')
        call(names[2], 0x80020009, wide=True)
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('path')
    asm.code += (path if isinstance(path, bytes) else path.encode('utf-8')) + b'\0'
    asm.label('fallback')
    asm.code += b'/app0/fallback.txt\0'
    return interfaces_fixture(asm.finish(), [(name, 'libc' if name == '_Exit' else library, 2,
                                 'libc' if name == '_Exit' else module) for name in names])


def rtc_fixture(date=None, *, tick=None, filetime=None, error=0, current_bounds=None, bad_pointer=False,
                clock_api='sceRtcGetCurrentTick'):
    names = [clock_api, 'sceRtcSetTick', 'sceRtcGetTick', 'sceRtcGetWin32FileTime', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(tick_out=4400, date_out=4440)
    asm.emit('48 83 ec 08')

    def call(name, expected=0):
        asm.relative('ff 15', name)
        asm.emit('3d ' + struct.pack('<I', expected).hex(' '))
        asm.relative('0f 85', 'fail')

    def check_qword(label, value, offset=0):
        asm.relative('48 8d 15', label)
        asm.emit('48 b8 ' + struct.pack('<Q', value).hex(' ') + ' 48 39 42 ' + bytes([offset]).hex())
        asm.relative('0f 85', 'fail')

    marker = 0xa5a5a5a5a5a5a5a5
    for label, size in (('tick_out', 16), ('date_out', 24)):
        asm.relative('48 8d 3d', label)
        asm.emit('48 b8 ' + struct.pack('<Q', marker).hex(' '))
        for offset in range(0, size, 8):
            asm.emit('48 89 47 ' + bytes([offset]).hex())
    if bad_pointer:
        pointer_error = 0x80b50002
        for name in names[:-1]:
            # Invalid input/output and a genuinely read-only output.
            for pointer in (0, 1):
                asm.emit('bf ' + struct.pack('<I', pointer).hex(' ') + ' be 01 00 00 00')
                call(name, pointer_error)
            asm.relative('48 8d 3d', 'date_constant')
            asm.relative('48 8d 35', 'tick_constant')
            call(name, pointer_error)
        check_qword('tick_out', marker)
        check_qword('date_out', marker)
    elif current_bounds is not None:
        asm.relative('48 8d 3d', 'tick_out')
        call(names[0])
        asm.relative('48 8b 15', 'tick_out')
        asm.emit('48 b8 ' + struct.pack('<Q', current_bounds[0]).hex(' ') + ' 48 39 c2')
        asm.relative('0f 82', 'fail')
        asm.emit('48 b8 ' + struct.pack('<Q', current_bounds[1]).hex(' ') + ' 48 39 c2')
        asm.relative('0f 87', 'fail')
        check_qword('tick_out', marker, 8)
        asm.relative('48 8d 3d', 'date_out')
        asm.relative('48 8d 35', 'tick_out')
        call(names[1])
        asm.relative('48 8d 3d', 'date_out')
        asm.relative('48 8d 35', 'date_out')  # Supported exact input/output alias: read before write.
        call(names[2])
        asm.relative('48 8b 05', 'tick_out')
        asm.relative('48 3b 05', 'date_out')
        asm.relative('0f 85', 'fail')
        check_qword('date_out', marker, 16)
    elif date is not None:
        for name, expected in ((names[2], tick), (names[3], filetime)):
            asm.relative('48 8d 3d', 'date_constant')
            asm.relative('48 8d 35', 'tick_out')
            call(name, error)
            check_qword('tick_out', marker if error else expected)
            check_qword('tick_out', marker, 8)
        if not error:
            asm.relative('48 8d 3d', 'date_out')
            asm.relative('48 8d 35', 'tick_constant')
            call(names[1])
            raw_date = struct.pack('<6HI', *date)
            for offset in (0, 8):
                check_qword('date_out', struct.unpack_from('<Q', raw_date, offset)[0], offset)
            check_qword('date_out', marker, 16)
    else:
        asm.relative('48 8d 3d', 'date_out')
        asm.relative('48 8d 35', 'tick_constant')
        call(names[1], error)
        for offset in (0, 8, 16):
            check_qword('date_out', marker, offset)
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('date_constant')
    asm.code += struct.pack('<6HI', *(date or (2000, 1, 1, 0, 0, 0, 0)))
    asm.label('tick_constant')
    asm.code += struct.pack('<Q', tick or 0)
    return interfaces_fixture(asm.finish(), [(name, 'libc' if name == '_Exit' else 'libSceRtc') for name in names])


def rtc_conversion_fixture(api, value=0, expected=None, *, error=0, alias=False, bad_pointer=None):
    asm = SyntheticCode()
    asm.labels.update(api=4112, exit_api=4120, buffer=4400)
    marker = 0xa5a5a5a5a5a5a5a5
    asm.emit('48 83 ec 08')
    asm.relative('48 8d 3d', 'buffer')
    asm.emit('48 b8 ' + struct.pack('<Q', marker).hex(' '))
    for offset in (0, 8, 16, 24):
        asm.emit('48 89 47 ' + bytes([offset]).hex())
    if api == 'sceRtcConvertUtcToLocalTime':
        asm.relative('48 8d 3d', 'buffer' if alias else 'constant')
        if alias:
            asm.emit('48 b8 ' + struct.pack('<Q', value).hex(' ') + ' 48 89 07')
        asm.relative('48 8d 35', 'buffer')
        if bad_pointer in ('input_null', 'input_invalid'):
            asm.emit('bf ' + struct.pack('<I', 0 if bad_pointer == 'input_null' else 1).hex(' '))
    elif api == 'sceRtcSetWin32FileTime':
        asm.relative('48 8d 3d', 'buffer')
        asm.emit('48 be ' + struct.pack('<Q', value).hex(' '))
    else:
        asm.relative('48 8d 3d', 'buffer')
    if bad_pointer and bad_pointer.startswith('output_'):
        register = '35' if api == 'sceRtcConvertUtcToLocalTime' else '3d'
        if bad_pointer == 'output_readonly':
            asm.relative('48 8d ' + register, 'constant')
        else:
            asm.emit(('be ' if api == 'sceRtcConvertUtcToLocalTime' else 'bf ') +
                     struct.pack('<I', 0 if bad_pointer == 'output_null' else 1).hex(' '))
    asm.relative('ff 15', 'api')
    asm.emit('3d ' + struct.pack('<I', error).hex(' '))
    asm.relative('0f 85', 'fail')
    raw = bytearray(struct.pack('<4Q', marker, marker, marker, marker))
    if alias:
        struct.pack_into('<Q', raw, 0, value)
    if not error:
        payload = struct.pack('<6HI', *expected) if isinstance(expected, tuple) else struct.pack('<Q', expected)
        raw[:len(payload)] = payload
    for offset in (0, 8, 16, 24):
        asm.relative('48 8d 15', 'buffer')
        asm.emit('48 b8 ' + raw[offset:offset + 8].hex(' ') + ' 48 39 42 ' + bytes([offset]).hex())
        asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit_api')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit_api')
    asm.label('constant')
    asm.code += struct.pack('<Q', value)
    return interfaces_fixture(asm.finish(), [(api, 'libSceRtc'), ('_Exit', 'libc')])


def libc_calendar_fixture(api, seconds=0, *, input_tm=None, expected_tm=None, expected_seconds=None,
                          bounds=None, error=None, bad_pointer=None, null_output=False):
    asm = SyntheticCode()
    asm.labels.update(api=4112, error_api=4120, exit_api=4128, buffer=4400)
    marker = 0xa5a5a5a5a5a5a5a5
    original = bytearray(struct.pack('<8Q', *([marker] * 8)))
    if input_tm is not None:
        original[:36] = struct.pack('<9i', *input_tm)
    asm.emit('48 83 ec 08')
    asm.relative('48 8d 3d', 'buffer')
    for offset in range(0, 64, 8):
        asm.emit('48 b8 ' + original[offset:offset + 8].hex(' ') + ' 48 89 47 ' + bytes([offset]).hex())
    asm.relative('ff 15', 'error_api')
    asm.emit('c7 00 49 00 00 00')
    asm.relative('48 8d 3d', 'buffer' if api in ('time', 'mktime') else 'constant')
    if null_output:
        asm.emit('31 ff')
    if bad_pointer is not None:
        if bad_pointer == 'readonly':
            asm.relative('48 8d 3d', 'constant')
        else:
            asm.emit('bf ' + struct.pack('<I', bad_pointer).hex(' '))
    asm.relative('ff 15', 'api')
    asm.emit('48 89 c3')  # Keep the complete RAX value across errno lookup.
    if error is not None:
        asm.emit('48 83 f8 ff' if api in ('time', 'mktime') else '48 85 c0')
        asm.relative('0f 85', 'fail')
    elif api in ('gmtime', 'localtime'):
        asm.emit('48 85 c0')
        asm.relative('0f 84', 'fail')
        for index, value in enumerate(expected_tm):
            asm.emit('81 78 ' + bytes([index * 4]).hex(' ') + ' ' + struct.pack('<i', value).hex(' '))
            asm.relative('0f 85', 'fail')
    elif bounds is not None:
        asm.emit('48 b8 ' + struct.pack('<q', bounds[0]).hex(' ') + ' 48 39 c3')
        asm.relative('0f 8c', 'fail')
        asm.emit('48 b8 ' + struct.pack('<q', bounds[1]).hex(' ') + ' 48 39 c3')
        asm.relative('0f 8f', 'fail')
    else:
        asm.emit('48 b8 ' + struct.pack('<q', expected_seconds).hex(' ') + ' 48 39 c3')
        asm.relative('0f 85', 'fail')
    asm.relative('ff 15', 'error_api')
    asm.emit('81 38 ' + struct.pack('<I', 73 if error is None else error).hex(' '))
    asm.relative('0f 85', 'fail')
    expected_buffer = bytearray(original)
    if api == 'mktime' and error is None:
        expected_buffer[:36] = struct.pack('<9i', *expected_tm)
    if api == 'time' and error is None and not null_output:
        asm.relative('48 8d 15', 'buffer')
        asm.emit('48 39 1a')
        asm.relative('0f 85', 'fail')
    for offset in range(8 if api == 'time' and error is None and not null_output else 0, 64, 8):
        asm.relative('48 8d 15', 'buffer')
        asm.emit('48 b8 ' + expected_buffer[offset:offset + 8].hex(' ') + ' 48 39 42 ' + bytes([offset]).hex())
        asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit_api')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit_api')
    asm.label('constant')
    asm.code += struct.pack('<9i', *input_tm) if input_tm is not None else struct.pack('<q', seconds)
    return interfaces_fixture(asm.finish(), [(api, 'libc'), ('__error', 'libkernel'), ('_Exit', 'libc')])


def offline_network_cleanup_fixture():
    imports = [('sceNetInit', 'libSceNet'), ('sceHttpTerm', 'libSceHttp'),
               ('sceSslTerm', 'libSceSsl'), ('sceNetPoolDestroy', 'libSceNet'),
               ('sceNetTerm', 'libSceNet'), ('_Exit', 'libc')]
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, (name, _) in enumerate(imports)})
    asm.emit('48 83 ec 08')

    def call(name, expected):
        asm.relative('ff 15', name)
        asm.emit('3d ' + struct.pack('<I', expected).hex(' '))
        asm.relative('0f 85', 'fail')

    # Teardown before init, failed init, then repeated destructor calls with
    # zero and never-issued nonzero IDs. None may be accepted as a resource.
    call('sceNetTerm', 0)
    for handle in (0, 73, 0xffffffff):
        call('sceNetInit', 0x8041014e)
        for name, expected in (('sceHttpTerm', 0x80431001), ('sceSslTerm', 0x8002004e),
                               ('sceNetPoolDestroy', 0x80410109)):
            asm.emit('bf ' + struct.pack('<I', handle).hex(' '))
            call(name, expected)
        call('sceNetTerm', 0)
    call('sceNetInit', 0x8041014e)
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    return interfaces_fixture(asm.finish(), imports)


def system_parameters_fixture():
    names = ['sceSystemServiceParamGetInt', 'sceSystemServiceReceiveEvent', 'sceSystemServicePowerTick', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(buffer=4400)
    asm.emit('48 83 ec 08')
    marker = 0xa5a5a5a5a5a5a5a5

    def call(name, expected=0):
        asm.relative('ff 15', name)
        asm.emit('3d ' + struct.pack('<I', expected).hex(' '))
        asm.relative('0f 85', 'fail')

    def check_marker(value=marker):
        asm.relative('48 8d 15', 'buffer')
        asm.emit('48 b8 ' + struct.pack('<Q', value).hex(' ') + ' 48 39 02')
        asm.relative('0f 85', 'fail')
        asm.emit('48 b8 ' + struct.pack('<Q', marker).hex(' ') + ' 48 39 42 08')
        asm.relative('0f 85', 'fail')

    for param in (1, 1000, 2, 0x7fffffff):
        asm.relative('48 8d 35', 'buffer')
        asm.emit('48 b8 ' + struct.pack('<Q', marker).hex(' ') + ' 48 89 06 48 89 46 08 48 ff c6')
        asm.emit('bf ' + struct.pack('<I', param).hex(' '))
        call(names[0], 0 if param in (1, 1000) else 0x80a10003)
        check_marker(0xa5a5a500000000a5 if param in (1, 1000) else marker)
    for pointer in (0, 1):
        asm.emit('bf 01 00 00 00 be ' + struct.pack('<I', pointer).hex(' '))
        call(names[0], 0x80a10003)
        asm.emit('bf ' + struct.pack('<I', pointer).hex(' '))
        call(names[1], 0x80a10003)
    # The buffer is unchanged after repeated empty dequeues.
    for _ in range(3):
        asm.relative('48 8d 3d', 'buffer')
        call(names[1], 0x80a10004)
        check_marker()
    call(names[2])
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    return interfaces_fixture(asm.finish(), [(name, 'libc' if name == '_Exit' else 'libSceSystemService') for name in names])


def system_status_fixture(*, library='libSceSystemService', module='libSceSystemService'):
    asm = SyntheticCode()
    asm.labels.update(status_slot=4112, exit_slot=4120, buffer=4400)
    asm.emit('48 83 ec 08')
    asm.relative('48 8d 3d', 'buffer')
    asm.emit('48 b8 ' + struct.pack('<Q', 0x7373737373737373).hex(' ') + ' 48 89 07 48 89 47 08')
    asm.relative('ff 15', 'status_slot')
    asm.emit('85 c0')
    asm.relative('0f 85', 'fail')
    asm.relative('48 8d 05', 'buffer')
    for raw in ('83 38 00', '81 78 04 00 00 01 00', '80 78 08 00', '80 78 09 73'):
        asm.emit(raw)
        asm.relative('0f 85', 'fail')
    asm.emit('31 ff')
    asm.relative('ff 15', 'status_slot')
    asm.emit('3d 03 00 a1 80')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit_slot')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit_slot')
    return interfaces_fixture(asm.finish(), [('sceSystemServiceGetStatus', library, 2, module), ('_Exit', 'libc')])


def addon_file_fixture(*, directory=False):
    names = ['sceSysmoduleLoadModule', 'sceAppContentInitialize', 'sceAppContentAddcontMount',
             'sceAppContentAddcontUnmount', 'sceKernelOpen', 'sceKernelClose', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(init=4400, boot=4440, point=4496)
    asm.emit('48 83 ec 08 bf b4 00 00 00')

    def call(name, expected=0):
        asm.relative('ff 15', name)
        asm.emit('3d ' + struct.pack('<I', expected).hex(' '))
        asm.relative('0f 85', 'fail')

    call(names[0])
    asm.relative('48 8d 3d', 'init')
    asm.relative('48 8d 35', 'boot')
    call(names[1])
    asm.emit('31 ff')
    asm.relative('48 8d 35', 'label')
    asm.relative('48 8d 15', 'point')
    call(names[2])
    asm.relative('48 8d 3d', 'path')
    asm.emit('be ' + struct.pack('<I', 0x20000 if directory else 0).hex(' ') + ' 31 d2')
    call(names[4], 3)
    asm.relative('48 8d 3d', 'point')
    call(names[3], 0x80d90003)
    asm.emit('bf 03 00 00 00')
    call(names[5])
    asm.relative('48 8d 3d', 'point')
    call(names[3])
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('label')
    asm.code += b'A000000000000001\0\0\0\0'
    asm.label('path')
    asm.code += b'/addcont0' + (b'' if directory else b'/payload.txt') + b'\0'
    imports = [(names[0], 'libSceSysmodule')]
    imports += [(name, 'libSceAppContent', 2, 'libSceAppContentUtil') for name in names[1:4]]
    imports += [(name, 'libkernel') for name in names[4:6]] + [('_Exit', 'libc')]
    return interfaces_fixture(asm.finish(), imports)


def gnm_init_fixture(*, mode='normal', library='libSceGnmDriver', module='libSceGnmDriver'):
    asm = SyntheticCode()
    asm.labels.update(init_slot=4112, exit_slot=4120, commands=4400, tail=5424)
    asm.emit('48 83 ec 08')
    asm.relative('c7 05', 'commands', '73 73 73 73')
    asm.relative('c7 05', 'tail', '73 73 73 73')
    asm.relative('48 8d 3d', 'commands')
    if mode == 'null':
        asm.emit('31 ff')
    elif mode == 'unaligned':
        asm.emit('48 ff c7')
    asm.emit('be ' + struct.pack('<I', 255 if mode == 'short' else (0x80000 if mode == 'large' else 256)).hex(' '))
    asm.relative('ff 15', 'init_slot')
    asm.emit('3d ' + struct.pack('<I', 256 if mode in ('normal', 'large') else 0).hex(' '))
    asm.relative('0f 85', 'fail')
    asm.relative('81 3d', 'commands', '00 28 01 c0' if mode in ('normal', 'large') else '73 73 73 73')
    asm.relative('0f 85', 'fail')
    asm.relative('81 3d', 'tail', '73 73 73 73')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit_slot')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit_slot')
    return interfaces_fixture(asm.finish(), [('sceGnmDrawInitDefaultHardwareState350', library, 2, module), ('_Exit', 'libc')])


def gnm_wait_fixture():
    names = ['sceVideoOutOpen', 'sceGnmInsertWaitFlipDone', 'sceVideoOutClose', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(commands=4400, tail=4428)
    asm.emit('48 83 ec 08 bf ff 00 00 00 31 f6 31 d2 31 c9')

    def call(name, expected=0):
        asm.relative('ff 15', name)
        asm.emit('3d ' + struct.pack('<I', expected & 0xffffffff).hex(' '))
        asm.relative('0f 85', 'fail')

    call(names[0], 1)
    asm.relative('c7 05', 'tail', '73 73 73 73')
    asm.relative('48 8d 3d', 'commands')
    asm.emit('be 07 00 00 00 ba 01 00 00 00 31 c9')
    call(names[1])
    asm.relative('48 8d 05', 'commands')
    for raw in ('81 38 00 3c 05 c0', '83 78 04 13', '83 78 10 00', '83 78 14 ff', '83 78 18 0a', '81 78 1c 73 73 73 73'):
        asm.emit(raw)
        asm.relative('0f 85', 'fail')
    asm.emit('f7 40 08 07 00 00 00')
    asm.relative('0f 85', 'fail')
    for size, handle, index in ((6, 1, 0), (7, 9, 0), (7, 1, 16)):
        asm.relative('48 8d 3d', 'commands')
        asm.emit('be ' + struct.pack('<I', size).hex(' ') + ' ba ' + struct.pack('<I', handle).hex(' ') + ' b9 ' + struct.pack('<I', index).hex(' '))
        call(names[1], -1)
    asm.emit('bf 01 00 00 00')
    call(names[2])
    asm.relative('48 8d 3d', 'commands')
    asm.emit('be 07 00 00 00 ba 01 00 00 00 31 c9')
    call(names[1], -1)
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    return interfaces_fixture(asm.finish(), [(names[0], 'libSceVideoOut'), (names[1], 'libSceGnmDriver'),
                                            (names[2], 'libSceVideoOut'), ('_Exit', 'libc')])


def roundf_fixture(*, library='libc', rounding=0):
    asm = SyntheticCode()
    asm.labels.update(round_slot=4112, exit_slot=4120, mxcsr=4400, altered=4404)
    asm.emit('48 83 ec 08')
    asm.relative('0f ae 1d', 'mxcsr')
    asm.relative('8b 05', 'mxcsr')
    asm.emit('25 ff 9f ff ff 0d ' + struct.pack('<I', rounding << 13).hex(' '))
    asm.relative('89 05', 'altered')
    asm.relative('0f ae 15', 'altered')
    for original, expected in ((0, 0), (0x80000000, 0x80000000), (0x3f000000, 0x3f800000),
                               (0xbf000000, 0xbf800000), (0x40200000, 0x40400000), (0xc0200000, 0xc0400000),
                               (0x3effffff, 0), (0xbeffffff, 0x80000000), (0x80000001, 0x80000000),
                               (0x7f800000, 0x7f800000), (0xff800000, 0xff800000), (0x7fc12345, None)):
        asm.emit('b8 ' + struct.pack('<I', original).hex(' ') + ' 66 0f 6e c0')
        asm.relative('ff 15', 'round_slot')
        asm.emit('66 0f 7e c0')
        if expected is None:
            asm.emit('25 ff ff ff 7f 3d 00 00 80 7f')
            asm.relative('0f 86', 'fail')
        else:
            asm.emit('3d ' + struct.pack('<I', expected).hex(' '))
            asm.relative('0f 85', 'fail')
    asm.relative('0f ae 15', 'mxcsr')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit_slot')
    asm.label('fail')
    asm.relative('0f ae 15', 'mxcsr')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit_slot')
    return interfaces_fixture(asm.finish(), [('roundf', library), ('_Exit', 'libc')])


def powf_fixture():
    asm = SyntheticCode()
    asm.labels.update(power=4112, exit=4120, error=4128)
    asm.emit('48 83 ec 08')
    asm.relative('ff 15', 'error')
    asm.emit('49 89 c4 41 c7 04 24 4d 00 00 00')
    cases = [(2.0, 3.0, 8.0), (0.5, -2.0, 4.0), (-2.0, 3.0, -8.0), (-2.0, 4.0, 16.0),
             (10.0, 0.25, math.pow(10, 0.25)), (2.0, -0.75, math.pow(2, -0.75)),
             (-0.0, 3.0, -0.0), (-0.0, 2.0, 0.0), (0.0, -1.0, math.inf), (-0.0, -3.0, -math.inf),
             (math.inf, -2.0, 0.0), (-math.inf, 3.0, -math.inf), (2.0, 128.0, math.inf), (2.0, -150.0, 0.0),
             (-2.0, 0.5, math.nan), (1.0, math.nan, math.nan), (math.nan, 0.0, 1.0),
             (1.0, math.inf, math.nan), (-1.0, -math.inf, math.nan)]
    for base, exponent, expected in cases:
        asm.emit('b8 ' + struct.pack('<f', base).hex(' ') + ' 66 0f 6e c0')
        asm.emit('b8 ' + struct.pack('<f', exponent).hex(' ') + ' 66 0f 6e c8')
        asm.relative('ff 15', 'power')
        asm.emit('66 0f 7e c0')
        if math.isnan(expected):
            asm.emit('25 ff ff ff 7f 3d 00 00 80 7f')
            asm.relative('0f 86', 'fail')
        elif expected == 0 or math.isinf(expected):
            asm.emit('3d ' + struct.pack('<f', expected).hex(' '))
            asm.relative('0f 85', 'fail')
        else:
            asm.emit('2d ' + struct.pack('<f', expected).hex(' ') + ' 83 c0 02 83 f8 04')
            asm.relative('0f 87', 'fail')
        asm.emit('41 83 3c 24 4d')
        asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit')
    return interfaces_fixture(asm.finish(), [('powf', 'libc'), ('_Exit', 'libc'), ('__error', 'libkernel')])


def logarithm_fixture(selectors=(0,), single=False):
    asm = SyntheticCode()
    asm.labels.update(logarithm=4112, exit=4120, error=4128)
    asm.emit('48 83 ec 08')
    asm.relative('ff 15', 'error')
    asm.emit('49 89 c4 41 c7 04 24 4d 00 00 00')
    packing = '<f' if single else '<d'
    tiny, large = (1e-30, 1e30) if single else (1e-300, 1e300)
    for value in (0.0, -0.0, -1.0, 1.0, 2.0, 4.0, 10.0, 1000.0, tiny, large, math.inf, -math.inf, math.nan):
        value = struct.unpack(packing, struct.pack(packing, value))[0]
        for selector in selectors:
            expected = (-math.inf if value == 0 else math.nan if value < 0 or math.isnan(value) else
                        math.log10(value) if selector > 0 else math.log2(value) if selector < 0 else math.log(value))
            asm.emit(('b8 ' if single else '48 b8 ') + struct.pack(packing, value).hex(' ') + (' 66 0f 6e c0' if single else ' 66 48 0f 6e c0'))
            asm.emit('bf ' + struct.pack('<i', selector).hex(' '))
            asm.relative('ff 15', 'logarithm')
            asm.emit('66 0f 7e c0' if single else '66 48 0f 7e c0')
            if math.isnan(expected):
                asm.emit('25 ff ff ff 7f 3d 00 00 80 7f' if single else '48 b9 ff ff ff ff ff ff ff 7f 48 21 c8 48 b9 00 00 00 00 00 00 f0 7f 48 39 c8')
                asm.relative('0f 86', 'fail')
            elif expected == 0 or math.isinf(expected):
                asm.emit(('3d ' if single else '48 b9 ') + struct.pack(packing, expected).hex(' ') + ('' if single else ' 48 39 c8'))
                asm.relative('0f 85', 'fail')
            else:
                asm.emit(('2d ' if single else '48 b9 ') + struct.pack(packing, expected).hex(' ') + (' 83 c0 02 83 f8 04' if single else ' 48 29 c8 48 83 c0 02 48 83 f8 04'))
                asm.relative('0f 87', 'fail')
            asm.emit('41 83 3c 24 4d')
            asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit')
    return interfaces_fixture(asm.finish(), [('_FLog' if single else '_Log', 'libc'), ('_Exit', 'libc'), ('__error', 'libkernel')])


def exp2_fixture():
    asm = SyntheticCode()
    asm.labels.update(exp2=4112, exit=4120, error=4128)
    asm.emit('48 83 ec 08')
    asm.relative('ff 15', 'error')
    asm.emit('49 89 c4 41 c7 04 24 4d 00 00 00')
    for value in (0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 10.0, -10.0, 1024.0, -1074.0, -1075.0, math.inf, -math.inf, math.nan):
        expected = math.nan if math.isnan(value) else math.inf if value >= 1024 else 0.0 if value < -1074 else math.exp2(value)
        asm.emit('48 b8 ' + struct.pack('<d', value).hex(' ') + ' 66 48 0f 6e c0')
        asm.relative('ff 15', 'exp2')
        asm.emit('66 48 0f 7e c0')
        if math.isnan(expected):
            asm.emit('48 b9 ff ff ff ff ff ff ff 7f 48 21 c8 48 b9 00 00 00 00 00 00 f0 7f 48 39 c8')
            asm.relative('0f 86', 'fail')
        elif value in (0.5, -0.5):
            asm.emit('48 b9 ' + struct.pack('<d', expected).hex(' ') + ' 48 29 c8 48 83 c0 02 48 83 f8 04')
            asm.relative('0f 87', 'fail')
        else:
            asm.emit('48 b9 ' + struct.pack('<d', expected).hex(' ') + ' 48 39 c8')
            asm.relative('0f 85', 'fail')
        asm.emit('41 83 3c 24 4d')
        asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit')
    return interfaces_fixture(asm.finish(), [('exp2', 'libc'), ('_Exit', 'libc'), ('__error', 'libkernel')])


def numerical_math_fixture(name, cases, *, single=True):
    asm = SyntheticCode()
    asm.labels.update(function=4112, exit=4120, error=4128)
    asm.emit('48 83 ec 08'); asm.relative('ff 15', 'error')
    asm.emit('49 89 c4 41 c7 04 24 4d 00 00 00')
    packing = '<f' if single else '<d'
    for arguments, expected in cases:
        for index, value in enumerate(arguments):
            asm.emit(('b8 ' if single else '48 b8 ') + struct.pack(packing, value).hex(' ') +
                     ((' 66 0f 6e c0' if index == 0 else ' 66 0f 6e c8') if single else
                      (' 66 48 0f 6e c0' if index == 0 else ' 66 48 0f 6e c8')))
        asm.relative('ff 15', 'function'); asm.emit('66 0f 7e c0' if single else '66 48 0f 7e c0')
        try:
            encoded = struct.pack(packing, expected)
        except OverflowError:
            expected = math.copysign(math.inf, expected); encoded = struct.pack(packing, expected)
        if math.isnan(expected):
            asm.emit('25 ff ff ff 7f 3d 00 00 80 7f' if single else
                     '48 b9 ff ff ff ff ff ff ff 7f 48 21 c8 48 b9 00 00 00 00 00 00 f0 7f 48 39 c8')
            asm.relative('0f 86', 'fail')
        elif expected == 0 or math.isinf(expected) or (single and abs(expected) < 2 ** -126):
            asm.emit(('3d ' if single else '48 b9 ') + encoded.hex(' ') + ('' if single else ' 48 39 c8'))
            asm.relative('0f 85', 'fail')
        else:
            asm.emit(('2d ' if single else '48 b9 ') + encoded.hex(' ') +
                     (' 83 c0 02 83 f8 04' if single else ' 48 29 c8 48 83 c0 02 48 83 f8 04'))
            asm.relative('0f 87', 'fail')
        asm.emit('41 83 3c 24 4d'); asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00'); asm.relative('ff 15', 'exit')
    asm.label('fail'); asm.emit('bf 63 00 00 00'); asm.relative('ff 15', 'exit')
    return interfaces_fixture(asm.finish(), [(name, 'libc'), ('_Exit', 'libc'), ('__error', 'libkernel')])


def net_offline_fixture(*, library='libSceNet', module='libSceNet'):
    asm = SyntheticCode()
    asm.labels.update(init_slot=4112, exit_slot=4120)
    asm.emit('48 83 ec 08')
    asm.relative('ff 15', 'init_slot')
    asm.emit('3d 4e 01 41 80')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit_slot')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit_slot')
    return interfaces_fixture(asm.finish(), [('sceNetInit', library, 2, module), ('_Exit', 'libc')])


def safe_area_fixture(*, library='libSceSystemService', module='libSceSystemService'):
    asm = SyntheticCode()
    asm.labels.update(area_slot=4112, exit_slot=4120, buffer=4400, tail=4404)
    asm.emit('48 83 ec 08')
    asm.relative('48 8d 3d', 'buffer')
    asm.emit('b9 8c 00 00 00 b0 73 f3 aa')
    asm.relative('48 8d 3d', 'buffer')
    asm.relative('ff 15', 'area_slot')
    asm.emit('85 c0')
    asm.relative('0f 85', 'fail')
    asm.relative('81 3d', 'buffer', '00 00 80 3f')
    asm.relative('0f 85', 'fail')
    asm.relative('48 8d 3d', 'tail')
    asm.emit('b9 88 00 00 00 b0 73 f3 ae')
    asm.relative('0f 85', 'fail')
    asm.emit('31 ff')
    asm.relative('ff 15', 'area_slot')
    asm.emit('3d 03 00 a1 80')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit_slot')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit_slot')
    return interfaces_fixture(asm.finish(), [('sceSystemServiceGetDisplaySafeAreaInfo', library, 2, module), ('_Exit', 'libc')])


def trophy_offline_fixture(*, library='libSceNpTrophy'):
    asm = SyntheticCode()
    asm.labels.update(create_slot=4112, exit_slot=4120, output=4400)
    asm.emit('48 83 ec 08')
    asm.relative('48 c7 05', 'output', '73 73 73 73')
    asm.relative('48 8d 3d', 'output')
    asm.relative('ff 15', 'create_slot')
    asm.emit('3d 01 16 55 80')
    asm.relative('0f 85', 'fail')
    asm.relative('48 81 3d', 'output', '73 73 73 73')
    asm.relative('0f 85', 'fail')
    asm.emit('31 ff')
    asm.relative('ff 15', 'create_slot')
    asm.emit('3d 04 16 55 80')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit_slot')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit_slot')
    return interfaces_fixture(asm.finish(), [('sceNpTrophyCreateHandle', library, 2, 'libSceNpTrophy'), ('_Exit', 'libc')])


def trig_fixture(*, double=False, library='libc'):
    asm = SyntheticCode()
    asm.labels.update(trig_slot=4112, exit_slot=4120)
    asm.emit('48 83 ec 08')
    packing, load, store = ('<d', '48 b8 ', '66 48 0f 6e c0') if double else ('<f', 'b8 ', '66 0f 6e c0')
    for value in (0.0, -0.0, 0.5, -1.0, math.pi / 2, math.pi, 12345.0, 1e20):
        raw = struct.pack(packing, value)
        actual_value = struct.unpack(packing, raw)[0]
        for cosine in (0, 1, -1, 2):
            expected = struct.pack(packing, math.cos(actual_value) if cosine else math.sin(actual_value))
            asm.emit(load + raw.hex(' ') + ' ' + store + ' bf ' + struct.pack('<i', cosine).hex(' '))
            asm.relative('ff 15', 'trig_slot')
            asm.emit('66 48 0f 7e c0' if double else '66 0f 7e c0')
            # Compare the same-sign IEEE encoding within two ULPs; no claim of
            # bit-identical target libm is made for the Windows math backend.
            if double:
                asm.emit('48 b9 ' + expected.hex(' ') + ' 48 29 c8 48 83 c0 02 48 83 f8 04')
            else:
                asm.emit('2d ' + expected.hex(' ') + ' 83 c0 02 83 f8 04')
            asm.relative('0f 87', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit_slot')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit_slot')
    return interfaces_fixture(asm.finish(), [('_Sin' if double else '_FSin', library), ('_Exit', 'libc')])


def gnm_shader_fixture(*, pixel=False, mode='normal', update=False, library='libSceGnmDriver', module=None):
    asm = SyntheticCode()
    words = 40 if pixel else 29
    success = mode in ('normal', 'large') or (pixel and mode == 'null')
    asm.labels.update(shader_slot=4112, exit_slot=4120, commands=4400, tail=4400 + words * 4, registers=4800)
    asm.emit('48 83 ec 08')
    asm.relative('48 8d 05', 'registers')
    for index, value in enumerate([0x12345678, 1 if mode == 'high_address' else 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]):
        asm.emit('c7 40 ' + struct.pack('<B', index * 4).hex(' ') + ' ' + struct.pack('<I', value).hex(' '))
    asm.relative('c7 05', 'commands', '73 73 73 73')
    asm.relative('c7 05', 'tail', '73 73 73 73')
    asm.relative('48 8d 3d', 'commands')
    if mode == 'unaligned':
        asm.emit('48 ff c7')
    asm.emit('be ' + struct.pack('<I', words - 1 if mode == 'short' else (128 if mode == 'large' else words)).hex(' '))
    if mode == 'null':
        asm.emit('31 d2')
    else:
        asm.relative('48 8d 15', 'registers')
    asm.emit('31 c9')
    asm.relative('ff 15', 'shader_slot')
    asm.emit('3d ' + struct.pack('<I', 0 if success else 0xffffffff).hex(' '))
    asm.relative('0f 85', 'fail')
    asm.relative('81 3d', 'commands', '00 76 02 c0' if success else '73 73 73 73')
    asm.relative('0f 85', 'fail')
    asm.relative('81 3d', 'tail', '73 73 73 73')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit_slot')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit_slot')
    name = ('sceGnmUpdatePsShader350' if pixel else 'sceGnmUpdateVsShader') if update else ('sceGnmSetPsShader350' if pixel else 'sceGnmSetVsShader')
    return interfaces_fixture(asm.finish(), [(name, library, 2, module or library), ('_Exit', 'libc')])


def gnm_draw_fixture(*, flags=0):
    asm = SyntheticCode()
    asm.labels.update(draw_slot=4112, exit_slot=4120, commands=4400, tail=4428)
    asm.emit('48 83 ec 08')
    asm.relative('c7 05', 'commands', '73 73 73 73')
    asm.relative('c7 05', 'tail', '73 73 73 73')
    asm.relative('48 8d 3d', 'commands')
    asm.emit('be 07 00 00 00 ba 03 00 00 00 b9 ' + struct.pack('<I', flags).hex(' '))
    asm.relative('ff 15', 'draw_slot')
    asm.emit('3d ' + struct.pack('<I', 0xffffffff if flags == 2 else 0).hex(' '))
    asm.relative('0f 85', 'fail')
    asm.relative('81 3d', 'commands', '73 73 73 73' if flags == 2 else '00 2d 01 c0')
    asm.relative('0f 85', 'fail')
    asm.relative('81 3d', 'tail', '73 73 73 73')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit_slot')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit_slot')
    return interfaces_fixture(asm.finish(), [('sceGnmDrawIndexAuto', 'libSceGnmDriver'), ('_Exit', 'libc')])


def gnm_draft_fixture(*, mode='normal'):
    names = ['sceKernelAllocateDirectMemory', 'sceKernelMapDirectMemory', 'sceGnmDrawInitDefaultHardwareState350',
             'sceGnmDrawIndexAuto', 'sceKernelMunmap', 'sceKernelReleaseDirectMemory', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(physical=4400, view=4408)
    asm.emit('48 83 ec 08 31 ff be 00 00 02 00 ba 00 00 02 00 b9 00 40 00 00 41 b8 03 00 00 00')
    asm.relative('4c 8d 0d', 'physical')

    def call(name, expected=0):
        asm.relative('ff 15', name)
        asm.emit('3d ' + struct.pack('<I', expected).hex(' '))
        asm.relative('0f 85', 'fail')

    call(names[0])
    asm.relative('48 8d 3d', 'view')
    asm.emit('be 00 00 02 00 ba 33 00 00 00 31 c9')
    asm.relative('4c 8b 05', 'physical')
    asm.emit('41 b9 00 40 00 00')
    call(names[1])
    asm.relative('4c 8b 25', 'view')
    asm.emit('4c 89 e7 be ' + struct.pack('<I', 256 if mode == 'claimed_short' else 32768).hex(' '))
    call(names[2], 256)
    for _ in range(2):
        asm.emit('49 8d bc 24 ' + struct.pack('<I', 0x11000 if mode == 'distant' else 0x400).hex(' '))
        asm.emit('be 07 00 00 00 ba 03 00 00 00 31 c9')
        call(names[3])
    asm.emit('4c 89 e7 be 00 00 02 00')
    call(names[4])
    asm.relative('48 8b 3d', 'physical')
    asm.emit('be 00 00 02 00')
    call(names[5])
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    return interfaces_fixture(asm.finish(), [(name, 'libc' if name == '_Exit' else 'libSceGnmDriver' if index in (2, 3) else 'libkernel')
                                            for index, name in enumerate(names)])


def gnm_submit_fixture(*, mode='normal', count=1, library='libSceGnmDriver', module='libSceGnmDriver', payload=None,
                       padded_words=0, declared_bytes=None, last_word=None):
    names = ['sceKernelAllocateDirectMemory', 'sceKernelMapDirectMemory', 'sceKernelMprotect',
             'sceGnmSubmitAndFlipCommandBuffers', '_Exit']
    asm = SyntheticCode()
    payload = ([0x80000000] if not padded_words else []) if payload is None else payload
    mapped_bytes = (max(0x20000, len(payload) * 4, padded_words * 4) + 0x3fff) & ~0x3fff
    stream_bytes = declared_bytes if declared_bytes is not None else (padded_words or len(payload)) * 4
    if mode == 'oversized':
        stream_bytes = 1024 * 1024 + 4
    elif mode == 'unaligned_size':
        stream_bytes = 3
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(physical=4400, view=4408, addresses=4600, sizes=4800, ordinary_data=4900)
    asm.emit('48 83 ec 28 31 ff be ' + struct.pack('<I', mapped_bytes).hex(' ') +
             ' ba ' + struct.pack('<I', mapped_bytes).hex(' ') + ' b9 00 40 00 00 41 b8 03 00 00 00')
    asm.relative('4c 8d 0d', 'physical')

    def call(index):
        asm.relative('ff 15', names[index])
        asm.emit('85 c0')
        asm.relative('0f 85', 'fail')

    call(0)
    asm.relative('48 8d 3d', 'view')
    asm.emit('be ' + struct.pack('<I', mapped_bytes).hex(' ') + ' ba 33 00 00 00 31 c9')
    asm.relative('4c 8b 05', 'physical')
    asm.emit('41 b9 00 40 00 00')
    call(1)
    asm.relative('48 8d 05' if mode == 'unregistered' else '48 8b 05', 'ordinary_data' if mode == 'unregistered' else 'view')
    if padded_words:
        asm.emit('48 89 c7 b9 ' + struct.pack('<I', padded_words).hex(' ') + ' b8 00 00 00 80 f3 ab')
        asm.relative('48 8b 05', 'view')  # restore pointer after REP STOSD
    for offset, word in enumerate(payload):
        asm.emit('c7 80 ' + struct.pack('<II', offset * 4, word).hex(' '))
    if last_word is not None:
        asm.emit('c7 80 ' + struct.pack('<II', (padded_words - 1) * 4, last_word).hex(' '))
    for index in range(min(count, 16)):
        asm.labels[f'address_{index}'] = 4600 + index * 8
        asm.labels[f'size_{index}'] = 4800 + index * 4
        asm.relative('48 89 05', f'address_{index}')
        asm.relative('c7 05', f'size_{index}', struct.pack('<I', stream_bytes).hex(' '))
    if mode == 'gpu_only':
        asm.relative('48 8b 3d', 'view')
        asm.emit('be ' + struct.pack('<I', mapped_bytes).hex(' ') + ' ba 30 00 00 00')
        call(2)
    asm.emit('c7 04 24 78 56 34 12 c7 44 24 08 10 32 54 76')
    asm.emit('48 b8 10 32 54 76 98 ba dc fe 48 89 44 24 10')
    asm.emit('bf ' + struct.pack('<I', count).hex(' '))
    if mode == 'bad_metadata':
        asm.emit('be 01 00 00 00')
    else:
        asm.relative('48 8d 35', 'addresses')
    asm.relative('48 8d 15', 'sizes')
    asm.emit('b9 01 00 00 00' if mode == 'ccb' else '31 c9')
    asm.emit('45 31 c0 41 b9 ef cd ab 89')
    call(3)  # inspection must not return a successful submission
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    return interfaces_fixture(asm.finish(), [(name, 'libc' if index == 4 else library if index == 3 else 'libkernel', 2,
                                             'libc' if index == 4 else module if index == 3 else 'libkernel') for index, name in enumerate(names)])


def condition_fixture(*, library='libkernel', module='libkernel'):
    names = ['scePthreadCondattrInit', 'scePthreadCondattrDestroy', 'scePthreadCondInit', 'scePthreadCondDestroy',
             'scePthreadCondSignal', 'scePthreadCondBroadcast', 'scePthreadCondWait', 'scePthreadMutexInit',
             'scePthreadMutexLock', 'scePthreadMutexUnlock', 'scePthreadCreate', 'scePthreadJoin', 'scePthreadMutexDestroy', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(attribute=4400, condition=4408, mutex=4416, thread=4424, result=4432, shared=4440)
    asm.emit('48 83 ec 08')

    def call(name, expected=0):
        asm.relative('ff 15', name)
        asm.emit('3d ' + struct.pack('<I', expected & 0xffffffff).hex(' '))
        asm.relative('0f 85', 'fail')

    asm.relative('48 8d 3d', 'attribute')
    call(names[0])
    asm.relative('48 8d 3d', 'condition')
    asm.relative('48 8d 35', 'attribute')
    asm.emit('31 d2')
    call(names[2])
    asm.relative('48 8d 3d', 'attribute')
    call(names[1])
    asm.relative('48 8d 3d', 'mutex')
    asm.emit('31 f6 31 d2')
    call(names[7])
    asm.relative('48 8d 3d', 'condition')
    asm.relative('48 8d 35', 'mutex')
    call(names[6], 0x80020001)  # does not own the mutex yet
    asm.relative('48 8d 3d', 'mutex')
    call(names[8])
    asm.relative('48 8d 3d', 'thread')
    asm.emit('31 f6')
    asm.relative('48 8d 15', 'worker')
    asm.relative('48 8d 0d', 'shared')
    asm.emit('45 31 c0')
    call(names[10])
    asm.relative('48 8d 3d', 'condition')
    asm.relative('48 8d 35', 'mutex')
    call(names[6])
    asm.relative('83 3d', 'shared', '49')
    asm.relative('0f 85', 'fail')
    asm.relative('48 8d 3d', 'mutex')
    call(names[8], 0x8002000b)  # ownership restored before wait returned
    asm.relative('48 8d 3d', 'mutex')
    call(names[9])
    asm.relative('48 8b 3d', 'thread')
    asm.relative('48 8d 35', 'result')
    call(names[11])
    asm.relative('48 8d 05', 'shared')
    asm.relative('48 39 05', 'result')
    asm.relative('0f 85', 'fail')
    for name in (names[4], names[5], names[3]):
        asm.relative('48 8d 3d', 'condition')
        call(name)
    asm.relative('48 8d 3d', 'mutex')
    call(names[12])
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('worker')
    asm.emit('48 83 ec 08 49 89 fc')
    asm.relative('48 8d 3d', 'mutex')
    call(names[8])
    asm.emit('41 c7 04 24 49 00 00 00')
    asm.relative('48 8d 3d', 'condition')
    call(names[4])
    asm.relative('48 8d 3d', 'mutex')
    call(names[9])
    asm.emit('4c 89 e0 48 83 c4 08 c3')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    return interfaces_fixture(asm.finish(), [(name, 'libc' if name == '_Exit' else library if index < 7 else 'libkernel', 2,
                                             'libc' if name == '_Exit' else module if index < 7 else 'libkernel') for index, name in enumerate(names)])


def thread_scheduling_fixture(*, library='libkernel', module='libkernel', inheritance=0):
    names = ['scePthreadAttrInit', 'scePthreadAttrSetinheritsched', 'scePthreadAttrSetschedparam', 'scePthreadAttrDestroy',
             'scePthreadCreate', 'scePthreadJoin', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(attribute=4400, thread=4408, result=4416, param=4424, sentinel=4428)
    asm.emit('48 83 ec 08')

    def call(name, expected=0):
        asm.relative('ff 15', name)
        asm.emit('3d ' + struct.pack('<I', expected & 0xffffffff).hex(' '))
        asm.relative('0f 85', 'fail')

    asm.relative('48 8d 3d', 'attribute')
    call(names[0])
    for mode in (7, inheritance):
        asm.relative('48 8d 3d', 'attribute')
        asm.emit('be ' + struct.pack('<I', mode).hex(' '))
        call(names[1], 0x8002002d if mode == 7 else 0)
    asm.relative('c7 05', 'sentinel', '73 73 73 73')
    for priority in (255, 699):
        asm.relative('c7 05', 'param', struct.pack('<I', priority).hex(' '))
        asm.relative('48 8d 3d', 'attribute')
        asm.relative('48 8d 35', 'param')
        call(names[2], 0x8002002d if priority == 255 else 0)
    asm.relative('48 8d 3d', 'thread')
    asm.relative('48 8d 35', 'attribute')
    asm.relative('48 8d 15', 'worker')
    asm.emit('48 b9 f0 de bc 9a 78 56 34 12 45 31 c0')
    call(names[4])
    asm.relative('48 8d 3d', 'attribute')
    call(names[3])
    asm.relative('48 8b 3d', 'thread')
    asm.relative('48 8d 35', 'result')
    call(names[5])
    asm.emit('48 b8 f0 de bc 9a 78 56 34 12')
    asm.relative('48 39 05', 'result')
    asm.relative('0f 85', 'fail')
    asm.relative('81 3d', 'sentinel', '73 73 73 73')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('worker')
    asm.emit('48 89 f8 c3')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    return interfaces_fixture(asm.finish(), [(name, 'libc' if name == '_Exit' else library if index in (1, 2) else 'libkernel', 2,
                                             'libc' if name == '_Exit' else module if index in (1, 2) else 'libkernel') for index, name in enumerate(names)])


def semaphore_fixture(*, library='libkernel', module='libkernel', options=False):
    names = ['sceKernelCreateSema', 'sceKernelWaitSema', 'sceKernelSignalSema', 'sceKernelPollSema',
             'sceKernelCancelSema', 'sceKernelDeleteSema', 'scePthreadCreate', 'scePthreadJoin', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(semaphore=4400, tail=4408, budget=4416, budget_tail=4420,
                      cancelled=4424, cancel_tail=4428, thread=4432, result=4440, shared=4448)
    asm.emit('48 83 ec 08')

    def call(index, expected=0):
        asm.relative('ff 15', names[index])
        asm.emit('3d ' + struct.pack('<I', expected & 0xffffffff).hex(' '))
        asm.relative('0f 85', 'fail')

    def check32(label, expected):
        asm.relative('81 3d', label, struct.pack('<I', expected).hex(' '))
        asm.relative('0f 85', 'fail')

    def handle(count=1):
        asm.relative('48 8b 3d', 'semaphore')
        asm.emit('be ' + struct.pack('<I', count & 0xffffffff).hex(' '))

    for label in ('tail', 'budget_tail', 'cancel_tail'):
        asm.relative('c7 05', label, '73 73 73 73')
    asm.relative('48 8d 3d', 'semaphore')
    asm.relative('48 8d 35', 'name')
    asm.emit('ba 01 00 00 00 31 c9 41 b8 03 00 00 00')
    if options:
        asm.relative('4c 8d 0d', 'budget')
    else:
        asm.emit('45 31 c9')
    call(0)  # all six SysV arguments, including the opaque options pointer
    asm.relative('48 8b 05', 'semaphore')
    asm.emit('48 c1 e8 20 48 85 c0')
    asm.relative('0f 84', 'fail')  # a qword handle, not a 32-bit slot
    check32('tail', 0x73737373)
    for count in (0, -1, 4):
        handle(count)
        asm.emit('31 d2')
        call(1, 0x80020016)
    handle()
    asm.relative('48 8d 15', 'budget')  # initially zero
    call(1, 0x8002003c)
    handle()
    call(3, 0x80020010)
    handle(2)
    call(2)
    asm.relative('c7 05', 'budget', struct.pack('<I', 12345).hex(' '))
    handle()
    asm.relative('48 8d 15', 'budget')
    call(1)
    check32('budget', 12345)  # immediate acquisition does not consume time budget
    handle()
    call(3)
    handle(3)
    call(2)
    handle()
    call(2, 0x80020016)  # overflow leaves the full count unchanged
    handle(3)
    call(3)
    handle(2)
    asm.relative('48 8d 15', 'cancelled')
    call(4)
    check32('cancelled', 0)
    check32('cancel_tail', 0x73737373)
    handle(2)
    call(3)
    asm.relative('c7 05', 'budget', struct.pack('<I', 20000).hex(' '))
    handle()
    asm.relative('48 8d 15', 'budget')
    call(1, 0x8002003c)
    check32('budget', 0)
    check32('budget_tail', 0x73737373)
    asm.relative('48 8d 3d', 'thread')
    asm.emit('31 f6')
    asm.relative('48 8d 15', 'worker')
    asm.relative('48 8d 0d', 'shared')
    asm.emit('45 31 c0')
    call(6)
    handle()
    asm.emit('31 d2')
    call(1)  # null timeout: an actual producer, not an unconditional success
    asm.relative('48 8b 3d', 'thread')
    asm.relative('48 8d 35', 'result')
    call(7)
    asm.relative('48 8d 05', 'shared')
    asm.relative('48 39 05', 'result')
    asm.relative('0f 85', 'fail')
    check32('shared', 73)
    handle()
    call(5)
    for index in (1, 2, 3, 4, 5):
        handle()
        asm.emit('31 d2')
        call(index, 0x80020003)
    check32('tail', 0x73737373)
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('worker')
    asm.emit('41 54 49 89 fc 41 c7 04 24 49 00 00 00')
    handle()
    call(2)
    asm.emit('4c 89 e0 41 5c c3')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('name')
    asm.emit((b'AuthoredSemaphore\0').hex(' '))
    return interfaces_fixture(asm.finish(), [(name, 'libc' if index == 8 else library if index < 6 else 'libkernel', 2,
                                             'libc' if index == 8 else module if index < 6 else 'libkernel') for index, name in enumerate(names)])


def affinity_fixture(*, library='libkernel', module='libkernel', mask=1):
    names = ['scePthreadAttrInit', 'scePthreadAttrSetaffinity', 'scePthreadCreate', 'scePthreadJoin', 'scePthreadAttrDestroy', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(attribute=4400, thread=4408, result=4416)
    asm.emit('48 83 ec 08')

    def call(index, expected=0):
        asm.relative('ff 15', names[index])
        asm.emit('3d ' + struct.pack('<I', expected & 0xffffffff).hex(' '))
        asm.relative('0f 85', 'fail')

    asm.relative('48 8d 3d', 'attribute')
    call(0)
    for selected, expected in ((0, 0x80020016), (1 << 32, 0x8002002d), (mask, 0)):
        asm.relative('48 8d 3d', 'attribute')
        asm.emit('48 be ' + struct.pack('<Q', selected).hex(' '))
        call(1, expected)
    asm.relative('48 8d 3d', 'thread')
    asm.relative('48 8d 35', 'attribute')
    asm.relative('48 8d 15', 'worker')
    asm.emit('48 b9 f0 de bc 9a 78 56 34 12 45 31 c0')
    call(2)
    asm.relative('48 8d 3d', 'attribute')
    call(4)
    asm.relative('48 8b 3d', 'thread')
    asm.relative('48 8d 35', 'result')
    call(3)
    asm.emit('48 b8 f0 de bc 9a 78 56 34 12')
    asm.relative('48 39 05', 'result')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('worker')
    asm.emit('48 89 f8 c3')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    return interfaces_fixture(asm.finish(), [(name, 'libc' if index == 5 else library if index == 1 else 'libkernel', 2,
                                             'libc' if index == 5 else module if index == 1 else 'libkernel') for index, name in enumerate(names)])


def synthetic_elf():
    raw = bytearray(0x300)
    ident = b"\x7fELF\x02\x01\x01" + bytes(9)
    struct.pack_into("<16sHHIQQQIHHHHHH", raw, 0, ident, 0xFE10, 62, 1,
                     0, 64, 0, 0, 64, 56, 3, 0, 0, 0)
    struct.pack_into("<II6Q", raw, 64, 1, 5, 0x100, 0, 0, 16, 4096, 4096)
    struct.pack_into("<II6Q", raw, 120, 2, 6, 0x180, 0, 0, 80, 80, 8)
    struct.pack_into("<II6Q", raw, 176, 0x61000000, 4, 0x200, 0, 0, 0x100, 0, 16)
    for i, item in enumerate(((0x61000035, 0), (0x61000037, 1),
                              (0x61000039, 8), (0x6100003F, 24), (0, 0))):
        struct.pack_into("<qQ", raw, 0x180 + i * 16, *item)
    raw[0x100:0x110] = bytes.fromhex("48 83 ec 08 ff 15 02 00 00 00 0f 0b 00 00 00 00")
    return raw


def synthetic_prx_elf(*, tls=False, duplicate=False, arrays=False, init=True, untyped_binding=None, referenced=False):
    raw = bytearray(0x800)
    struct.pack_into('<16sHHIQQQIHHHHHH', raw, 0, b'\x7fELF\x02\x01\x01' + bytes(9), 0xFE18, 62, 1,
                     0, 64, 0, 0, 64, 56, 4 if tls else 3, 0, 0, 0)
    struct.pack_into('<II6Q', raw, 64, 1, 5, 0x200, 0, 0, 64, 4096, 4096)
    strings = b'\0libFixture\0' + (nid('TestValue') + '#A#A').encode() + b'\0'
    untyped_name = len(strings)
    if untyped_binding is not None:
        strings += b'OptionalFixtureSymbol\0'
    tags = [(0x61000035, 0), (0x61000037, len(strings)), (0x61000039, 0x80),
            (0x6100003F, 72 if duplicate or untyped_binding is not None else 48), (12 if init else 0x7000, 0), (13, 16),
            (0x61000013, 1), (0x6100000D, 1)]
    if arrays:
        tags.append((27, 8))
    if referenced:
        tags += [(0x6100002F, 0xe0), (0x61000031, 24)]
    tags.append((0, 0))
    struct.pack_into('<II6Q', raw, 120, 2, 6, 0x300, 0, 0, len(tags) * 16, len(tags) * 16, 8)
    struct.pack_into('<II6Q', raw, 176, 0x61000000, 4, 0x500, 0, 0, 0x100, 0, 16)
    if tls:
        struct.pack_into('<II6Q', raw, 232, 7, 4, 0x240, 0, 0, 1, 16, 16)
    raw[0x200:0x203] = bytes.fromhex('31 c0 c3')
    for index, item in enumerate(tags):
        struct.pack_into('<qQ', raw, 0x300 + index * 16, *item)
    raw[0x500:0x500 + len(strings)] = strings
    for index in range(1, 3 if duplicate else 2):
        struct.pack_into('<IBBHQQ', raw, 0x580 + 24 * index, 12, 0x12, 0, 1, 32, 1)
    if untyped_binding is not None:
        struct.pack_into('<IBBHQQ', raw, 0x5b0, untyped_name, untyped_binding << 4, 0, 0, 0, 0)
    if referenced:
        struct.pack_into('<QQq', raw, 0x5e0, 48, (2 << 32) | 6, 0)
    return raw


def module_preparer():
    spec = importlib.util.spec_from_file_location('kt_prepare_module_tests', ROOT / 'scripts/prepare-native.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def prepared_module_load_fixture():
    asm = SyntheticCode()
    names = ['sceKernelLoadStartModule', '_Exit']
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels['result'] = 4400
    asm.emit('48 83 ec 08')
    asm.relative('48 8d 3d', 'path')
    asm.emit('31 f6 31 d2 31 c9 45 31 c0')
    asm.relative('4c 8d 0d', 'result')
    asm.relative('ff 15', names[0])
    asm.emit('85 c0')
    asm.relative('0f 8e', 'fail')
    asm.relative('83 3d', 'result', '00')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', names[1])
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', names[1])
    asm.label('path')
    asm.code += b'/app0/simple.prx\0'
    return interfaces_fixture(asm.finish(), [(names[0], 'libkernel'), (names[1], 'libc')], data_size=512)


def decoded_test_png(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Not a PNG")
    cursor, compressed = 8, bytearray()
    width = height = None
    while cursor < len(data):
        size = struct.unpack_from(">I", data, cursor)[0]
        kind = data[cursor + 4:cursor + 8]
        payload = data[cursor + 8:cursor + 8 + size]
        checksum = struct.unpack_from(">I", data, cursor + 8 + size)[0]
        if zlib.crc32(kind + payload) != checksum:
            raise ValueError("Invalid PNG CRC")
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">2I5B", payload)
            if (depth, color, compression, filtering, interlace) != (8, 6, 0, 0, 0):
                raise ValueError("Expected non-interlaced RGBA8 fixture output")
        elif kind == b"IDAT":
            compressed += payload
        cursor += size + 12
        if kind == b"IEND":
            break
    if cursor != len(data) or width is None:
        raise ValueError("Incomplete PNG")
    packed = zlib.decompress(compressed)
    row_size = width * 4
    if len(packed) != height * (row_size + 1):
        raise ValueError("Invalid PNG pixel extent")
    previous = bytearray(row_size)
    pixels = bytearray()
    for row in range(height):
        start = row * (row_size + 1)
        filtering = packed[start]
        current = bytearray(packed[start + 1:start + 1 + row_size])
        for index in range(row_size):
            left = current[index - 4] if index >= 4 else 0
            above = previous[index]
            upper_left = previous[index - 4] if index >= 4 else 0
            if filtering == 0:
                prediction = 0
            elif filtering == 1:
                prediction = left
            elif filtering == 2:
                prediction = above
            elif filtering == 3:
                prediction = (left + above) // 2
            elif filtering == 4:
                predictor = left + above - upper_left
                distances = [abs(predictor - value) for value in (left, above, upper_left)]
                prediction = (left, above, upper_left)[distances.index(min(distances))]
            else:
                raise ValueError("Invalid PNG row filter")
            current[index] = (current[index] + prediction) & 255
        pixels += current
        previous = current
    return width, height, bytes(pixels)


def synthetic_probe(symbol_type=2, identifier="fixture_nid#A#A", name="synthetic_test",
                    library="fixture_library", module="fixture_module"):
    if symbol_type == 2:
        code = bytes.fromhex("48 83 ec 08 ff 15 02 00 00 00 0f 0b") + bytes(8)
        target = 12
    else:
        code = bytes.fromhex("48 83 ec 08 48 8b 05 05 00 00 00 48 8b 00 0f 0b") + bytes(8)
        target = 16
    result = bytearray(struct.pack("<8s7Q32s", b"KTPROBE1", 4096, 0, 1, 1, 1, 0, 0, bytes(32)))
    result += struct.pack("<4Q", 0, 4096, 5, len(code)) + code
    for field in (identifier, name, library, module):
        value = field.encode("ascii")
        result += struct.pack("<I", len(value)) + value
    result += struct.pack("<I", symbol_type)
    result += struct.pack("<3Qq", target, 1, 0, 0)
    return result


class SyntheticCode:
    def __init__(self):
        self.code = bytearray()
        self.labels = {"counter": 4096, "atexit_slot": 4112, "exit_slot": 4120, "immediate_slot": 4128}
        self.fixups = []

    def emit(self, data):
        self.code += bytes.fromhex(data)

    def label(self, name):
        self.labels[name] = len(self.code)

    def relative(self, prefix, name, suffix=""):
        self.emit(prefix)
        at = len(self.code)
        self.code += bytes(4)
        self.emit(suffix)
        self.fixups.append((at, name, len(self.code)))

    def finish(self):
        for at, name, after in self.fixups:
            struct.pack_into("<i", self.code, at, self.labels[name] - after)
        return self.code


def exit_fixture(immediate=False):
    asm = SyntheticCode()
    asm.emit("48 83 ec 08")
    for callback in ("first", "second"):
        asm.relative("48 8d 3d", callback)
        asm.relative("ff 15", "atexit_slot")
        asm.emit("85 c0")
        asm.relative("0f 85", "fail")
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "immediate_slot" if immediate else "exit_slot")
    asm.emit("0f 0b")
    for name, expected, value in (("first", "02", "01"), ("second", "00", "02")):
        asm.label(name)
        asm.relative("83 3d", "counter", expected)
        asm.relative("0f 85", "fail")
        asm.relative("c7 05", "counter", value + " 00 00 00")
        asm.emit("c3")
    asm.label("fail")
    asm.emit("48 83 e4 f0 bf 63 00 00 00")
    asm.relative("ff 15", "immediate_slot")
    asm.emit("0f 0b")
    code = asm.finish()
    imports = [("8G2LB+A3rzg#A#A", "atexit"), ("uMei1W9uyNo#A#A", "exit"),
               ("wZi5ly2guNw#A#A", "_Exit")]
    result = bytearray(struct.pack("<8s7Q32s", b"KTPROBE1", 8192, 0, 2, 3, 3, 0, 0, bytes(32)))
    result += struct.pack("<4Q", 0, 4096, 5, len(code)) + code
    result += struct.pack("<4Q", 4096, 4096, 6, 64) + bytes(64)
    for identifier, name in imports:
        for value in (identifier, name, "libc", "libc"):
            value = value.encode("ascii")
            result += struct.pack("<I", len(value)) + value
        result += struct.pack("<I", 2)
    for index in range(3):
        result += struct.pack("<3Qq", 4112 + 8 * index, 1, index, 0)
    return result


def canary_fixture(write=False):
    asm = SyntheticCode()
    asm.labels.update(guard_slot=4112, exit_slot=4120)
    asm.emit("48 83 ec 08")
    asm.relative("4c 8b 15", "guard_slot")
    asm.emit("49 8b 02 48 85 c0")
    asm.relative("0f 84", "fail")
    if write:
        asm.emit("49 89 02")
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "exit_slot")
    asm.emit("0f 0b")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "exit_slot")
    asm.emit("0f 0b")
    code = asm.finish()
    result = bytearray(struct.pack("<8s7Q32s", b"KTPROBE1", 8192, 0, 2, 2, 2, 0, 0, bytes(32)))
    result += struct.pack("<4Q", 0, 4096, 5, len(code)) + code
    result += struct.pack("<4Q", 4096, 4096, 6, 64) + bytes(64)
    for identifier, name, library, kind in (("f7uOxY9mM1U#A#A", "__stack_chk_guard", "libkernel", 1),
                                            ("uMei1W9uyNo#A#A", "exit", "libc", 2)):
        for field in (identifier, name, library, library):
            value = field.encode("ascii")
            result += struct.pack("<I", len(value)) + value
        result += struct.pack("<I", kind)
    for index in range(2):
        result += struct.pack("<3Qq", 4112 + 8 * index, 1, index, 0)
    return result


def allocation_fixture(mode="normal", array=False, nothrow=False, c_allocation=False):
    asm = SyntheticCode()
    asm.labels.update(new_slot=4112, delete_slot=4120, immediate_slot=4128)
    asm.emit("48 83 ec 08")
    if mode == "invalid":
        asm.emit("bf 34 12 00 00")
        asm.relative("ff 15", "delete_slot")
        asm.relative("e9", "fail")
    elif mode == "oom":
        asm.emit("48 c7 c7 ff ff ff ff 31 f6")
        asm.relative("ff 15", "new_slot")
        asm.emit("48 85 c0")
        asm.relative("0f 85", "fail")
    else:
        asm.emit("bf 20 00 00 00 31 f6")
        asm.relative("ff 15", "new_slot")
        asm.emit("48 85 c0")
        asm.relative("0f 84", "fail")
        asm.emit("a8 0f")
        asm.relative("0f 85", "fail")
        asm.emit("49 89 c4 48 b8 78 56 34 12 ef cd ab 09 49 89 04 24 49 89 44 24 18")
        asm.emit("31 ff 31 f6")  # zero-byte new must provide distinct usable storage
        asm.relative("ff 15", "new_slot")
        asm.emit("48 85 c0")
        asm.relative("0f 84", "fail")
        asm.emit("a8 0f")
        asm.relative("0f 85", "fail")
        asm.emit("49 39 c4")
        asm.relative("0f 84", "fail")
        asm.emit("49 89 c5 c6 00 5a 48 b8 78 56 34 12 ef cd ab 09 49 39 04 24")
        asm.relative("0f 85", "fail")
        asm.emit("49 39 44 24 18")
        asm.relative("0f 85", "fail")
        asm.emit("4c 89 e7")
        asm.relative("ff 15", "delete_slot")
        if mode == "double":
            asm.emit("4c 89 e7")
            asm.relative("ff 15", "delete_slot")
            asm.relative("e9", "fail")
        asm.emit("4c 89 ef")
        asm.relative("ff 15", "delete_slot")
        asm.emit("31 ff")
        asm.relative("ff 15", "delete_slot")
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "immediate_slot")
    asm.emit("0f 0b")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "immediate_slot")
    asm.emit("0f 0b")
    code = asm.finish()
    new_name = "malloc" if c_allocation else (
        ("_Znam" if array else "_Znwm") + ("RKSt9nothrow_t" if nothrow else ""))
    imports = [new_name, "free" if c_allocation else ("_ZdaPv" if array else "_ZdlPv"), "_Exit"]
    result = bytearray(struct.pack("<8s7Q32s", b"KTPROBE1", 8192, 0, 2, 3, 3, 0, 0, bytes(32)))
    result += struct.pack("<4Q", 0, 4096, 5, len(code)) + code
    result += struct.pack("<4Q", 4096, 4096, 6, 64) + bytes(64)
    for name in imports:
        for field in (nid(name) + "#A#A", name, "libc", "libc"):
            value = field.encode("ascii")
            result += struct.pack("<I", len(value)) + value
        result += struct.pack("<I", 2)
    for index in range(3):
        result += struct.pack("<3Qq", 4112 + 8 * index, 1, index, 0)
    return result


def cxa_fixture(scoped=False, recursive=False):
    asm = SyntheticCode()
    asm.labels.update(cxa_slot=4112, finalize_slot=4120, exit_slot=4128, immediate_slot=4136,
                      atexit_slot=4144, dso_a=4160, dso_b=4168)
    asm.emit("48 83 ec 08")
    asm.relative("48 8d 3d", "plain")
    asm.relative("ff 15", "atexit_slot")
    asm.emit("85 c0")
    asm.relative("0f 85", "fail")
    for callback, dso in (("first", "dso_a"), ("second", "dso_b"), ("third", "dso_a")):
        asm.relative("48 8d 3d", callback)
        asm.relative("48 8d 35", "counter")
        asm.relative("48 8d 15", dso)
        asm.relative("ff 15", "cxa_slot")
        asm.emit("85 c0")
        asm.relative("0f 85", "fail")
    if scoped:
        for _ in range(2):  # repeated DSO finalize must not invoke entries twice
            asm.relative("48 8d 3d", "dso_a")
            asm.relative("ff 15", "finalize_slot")
        asm.relative("83 3d", "counter", "02")
        asm.relative("0f 85", "fail")
    else:
        for _ in range(2):
            asm.emit("31 ff")
            asm.relative("ff 15", "finalize_slot")
        asm.relative("83 3d", "counter", "04")
        asm.relative("0f 85", "fail")
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "exit_slot")
    asm.emit("0f 0b")
    for name, expected in (("third", 0), ("second", 2 if scoped else 1),
                           ("first", 1 if scoped else 2), ("plain", 3)):
        asm.label(name)
        if name != "plain":
            asm.relative("48 8d 05", "counter")
            asm.emit("48 39 c7")
            asm.relative("0f 85", "fail")
        asm.relative("83 3d", "counter", f"{expected:02x}")
        asm.relative("0f 85", "fail")
        asm.relative("c7 05", "counter", f"{expected + 1:02x} 00 00 00")
        if name == "third" and recursive:
            asm.emit("48 83 ec 08 31 ff")
            asm.relative("ff 15", "finalize_slot")
            asm.emit("48 83 c4 08")
        asm.emit("c3")
    asm.label("fail")
    asm.emit("48 83 e4 f0 bf 63 00 00 00")
    asm.relative("ff 15", "immediate_slot")
    asm.emit("0f 0b")
    code = asm.finish()
    imports = ["__cxa_atexit", "__cxa_finalize", "exit", "_Exit", "atexit"]
    result = bytearray(struct.pack("<8s7Q32s", b"KTPROBE1", 8192, 0, 2, 5, 5, 0, 0, bytes(32)))
    result += struct.pack("<4Q", 0, 4096, 5, len(code)) + code
    result += struct.pack("<4Q", 4096, 4096, 6, 128) + bytes(128)
    for name in imports:
        for field in (nid(name) + "#A#A", name, "libc", "libc"):
            value = field.encode("ascii")
            result += struct.pack("<I", len(value)) + value
        result += struct.pack("<I", 2)
    for index in range(len(imports)):
        result += struct.pack("<3Qq", 4112 + 8 * index, 1, index, 0)
    return result


def interfaces_fixture(code, imports, data_size=128):
    data_memory = max(4096, (data_size + 4095) // 4096 * 4096)
    result = bytearray(struct.pack("<8s7Q32s", b"KTPROBE1", 4096 + data_memory, 0, 2,
                                  len(imports), len(imports), 0, 0, bytes(32)))
    result += struct.pack("<4Q", 0, 4096, 5, len(code)) + code
    result += struct.pack("<4Q", 4096, data_memory, 6, data_size) + bytes(data_size)
    for item in imports:
        name, library = item[:2]
        module = item[3] if len(item) > 3 else library
        for field in (nid(name) + "#A#A", name, library, module):
            value = field.encode("ascii")
            result += struct.pack("<I", len(value)) + value
        result += struct.pack("<I", item[2] if len(item) > 2 else 2)
    for index in range(len(imports)):
        result += struct.pack("<3Qq", 4112 + 8 * index, 1, index, 0)
    return result


def atof_fixture(*, library='libc', module='libc'):
    cases = [(' \t-12.5tail', -12.5, 73), ('0x1.8p+1', 3.0, 73), ('1.25e2', 125.0, 73),
             ('not-a-number', 0.0, 73), ('-0', -0.0, 73), ('9007199254740991', 9007199254740991.0, 73),
             ('1e100', 1e100, 73), ('1e9999', math.inf, 34), ('1e-9999', 0.0, 34),
             ('-INF', -math.inf, 73), ('NAN', math.nan, 73)]
    asm = SyntheticCode()
    asm.labels.update(atof=4112, exit=4120, error=4128)
    asm.emit('48 83 ec 08')
    asm.relative('ff 15', 'error')
    asm.emit('49 89 c4')
    for index, (value, expected, error) in enumerate(cases):
        failure = f'atof_fail_{index}'
        asm.emit('41 c7 04 24 49 00 00 00')
        asm.relative('48 8d 3d', f'text_{index}')
        asm.emit('48 c7 c6 ff ff ff ff')  # atof must not treat RSI as an end pointer
        asm.relative('ff 15', 'atof')
        asm.emit('66 48 0f 7e c0')  # full double return from XMM0
        if math.isnan(expected):
            asm.emit('48 b9 ff ff ff ff ff ff ff 7f 48 21 c8 48 b9 00 00 00 00 00 00 f0 7f 48 39 c8')
            asm.relative('0f 86', failure)
        else:
            asm.emit('48 b9 ' + struct.pack('<d', expected).hex(' ') + ' 48 39 c8')
            asm.relative('0f 85', failure)
        asm.emit('41 83 3c 24 ' + bytes([error]).hex(' '))
        asm.relative('0f 85', 'atof_errno_fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit')
    for index in range(len(cases)):
        asm.label(f'atof_fail_{index}')
        asm.emit('bf ' + struct.pack('<I', 65 + index).hex(' '))
        asm.relative('ff 15', 'exit')
    asm.label('atof_errno_fail')
    asm.emit('41 8b 3c 24 81 c7 80 00 00 00')
    asm.relative('ff 15', 'exit')
    for index, (value, _, _) in enumerate(cases):
        asm.label(f'text_{index}')
        asm.code.extend(value.encode('ascii') + b'\0')
    return interfaces_fixture(asm.finish(), [('atof', library, 2, module), ('_Exit', 'libc'), ('__error', 'libkernel')])


def strtof_fixture(*, library='libc', module='libc'):
    cases = [(' \t-12.5tail', 0xc1480000, 73, 7), ('0x1.8p+1tail', 0x40400000, 73, 8),
             ('1.0000000596046448', 0x3f800001, 73, 18), ('bad', 0, 73, 0),
             ('-0', 0x80000000, 73, 2), ('1e99', 0x7f800000, 34, 4),
             ('1e-99', 0, 34, 5), ('-INF', 0xff800000, 73, 4)]
    asm = SyntheticCode()
    asm.labels.update(strtof=4112, exit=4120, error=4128, end=4400)
    asm.emit('48 83 ec 08')
    asm.relative('ff 15', 'error')
    asm.emit('49 89 c4')
    for index, (text, expected, error, consumed) in enumerate(cases):
        asm.emit('41 c7 04 24 49 00 00 00')
        asm.relative('48 8d 3d', f'text_{index}')
        asm.relative('48 8d 35', 'end')
        asm.relative('ff 15', 'strtof')
        asm.emit('66 0f 7e c0 3d ' + struct.pack('<I', expected).hex(' '))
        asm.relative('0f 85', 'fail')
        asm.emit('41 83 3c 24 ' + bytes([error]).hex(' '))
        asm.relative('0f 85', 'fail')
        asm.relative('48 8d 05', f'text_{index}')
        asm.emit('48 83 c0 ' + bytes([consumed]).hex(' '))
        asm.relative('48 3b 05', 'end')
        asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit')
    for index, (text, _, _, _) in enumerate(cases):
        asm.label(f'text_{index}')
        asm.code.extend(text.encode() + b'\0')
    return interfaces_fixture(asm.finish(), [('strtof', library, 2, module), ('_Exit', 'libc'), ('__error', 'libkernel')])


def qsort_fixture(*, library='libc', module='libc', mode='normal'):
    asm = SyntheticCode()
    asm.labels.update(qsort=4112, exit=4120, array=4400)
    asm.emit('48 83 ec 08')
    if mode == 'normal':
        asm.relative('48 8d 3d', 'array')
        asm.emit('be 07 00 00 00 ba 04 00 00 00')
        asm.relative('48 8d 0d', 'comparison')
    else:
        asm.emit('31 ff 31 c9')
        asm.emit('48 be ' + struct.pack('<Q', {'empty': 0, 'one': 1, 'zero_width': 0xffffffffffffffff,
                                                'overflow': 0xffffffffffffffff}[mode]).hex(' '))
        asm.emit('ba ' + struct.pack('<I', 0 if mode == 'zero_width' else 4).hex(' '))
        if mode == 'overflow':
            asm.emit('bf 01 00 00 00')
            asm.relative('48 8d 0d', 'comparison')
    asm.relative('ff 15', 'qsort')
    if mode == 'normal':
        for index, expected in enumerate((-2147483648, -7, -1, 0, 7, 7, 2147483647)):
            asm.labels[f'element_{index}'] = 4400 + index * 4
            asm.relative('81 3d', f'element_{index}', struct.pack('<i', expected).hex(' '))
            asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit')
    asm.label('comparison')
    asm.emit('8b 07 3b 06 0f 9f c0 0f 9c c2 0f b6 c0 0f b6 d2 29 d0 c3')
    code = asm.finish()
    image = interfaces_fixture(code, [('qsort', library, 2, module), ('_Exit', 'libc')], data_size=512)
    data_start = 96 + 32 + len(code) + 32
    struct.pack_into('<7i', image, data_start + 4400 - 4096, 2147483647, -1, 0, -2147483648, 7, 7, -7)
    return image


def vag_parse_fixture(action='valid', *, library='libSceNgs2', module='libSceNgs2'):
    payload = bytearray(80)
    payload[:4] = b'VAGp'
    for offset, value in ((4, 0x20001), (12, 32), (16, 48000)):
        struct.pack_into('>I', payload, offset, value)
    payload[30] = 1
    payload[64] = 12
    payload[66:] = bytes([0x11]) * 14
    expected = 0
    length = len(payload)
    if action == 'short':
        length, expected = 15, 0x804a0408
    elif action == 'zero_size':
        length, expected = 0, 0x804a0407
    elif action == 'bad_pointer':
        expected = 0x804a0406
    elif action == 'null_output':
        expected = 0x804a0053
    elif action == 'bad_rate':
        struct.pack_into('>I', payload, 16, 7000)
        expected = 0x804a0408
    elif action == 'bad_predictor':
        payload[48:50] = bytes([0xf0, 0xf0])
        expected = 0x804a0408
    elif action == 'loop':
        payload[49] = 6
    asm = SyntheticCode()
    asm.labels.update(parse=4112, exit=4120, output=4400, tail=4600)
    asm.emit('48 83 ec 08 48 b8 a5 a5 a5 a5 a5 a5 a5 a5')
    asm.relative('48 8d 3d', 'output')
    asm.emit('b9 1a 00 00 00 f3 48 ab')  # 200-byte info plus 8-byte tail
    if action == 'bad_pointer':
        asm.emit('bf 01 00 00 00')
    else:
        asm.relative('48 8d 3d', 'vag')
    asm.emit('be ' + struct.pack('<I', length).hex(' '))
    if action == 'null_output':
        asm.emit('31 d2')
    else:
        asm.relative('48 8d 15', 'output')
    asm.relative('ff 15', 'parse')
    asm.emit('3d ' + struct.pack('<I', expected).hex(' '))
    asm.relative('0f 85', 'fail')
    output = bytes([0xa5]) * 200
    if action == 'valid':
        output = struct.pack('<18I', 0x4b485631, 1, 48000, 0, 0, 0,
                             48, 32, 0, 0, 56, 16, 28, 1, 16, 28, 0, 1)
        output += struct.pack('<6IQ', 48, 32, 0, 0, 56, 0, 0) + bytes(96)
    for index in range(50):
        label = f'output_{index}'
        asm.labels[label] = 4400 + index * 4
        asm.relative('81 3d', label, output[index * 4:index * 4 + 4].hex(' '))
        asm.relative('0f 85', 'fail')
    asm.relative('48 8b 05', 'tail')
    asm.emit('48 b9 a5 a5 a5 a5 a5 a5 a5 a5 48 39 c8')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit')
    asm.label('vag')
    asm.code.extend(payload)
    return interfaces_fixture(asm.finish(), [('sceNgs2ParseWaveformData', library, 2, module), ('_Exit', 'libc')], data_size=512)


def puts_fixture(*, library='libc', module='libc', invalid=False):
    asm = SyntheticCode()
    asm.labels.update(puts=4112, exit=4120)
    asm.emit('48 83 ec 08')
    if invalid:
        asm.emit('bf 01 00 00 00')
    else:
        asm.relative('48 8d 3d', 'text')
    asm.relative('ff 15', 'puts')
    asm.emit('85 c0')
    asm.relative('0f 88', 'fail')
    asm.relative('48 8d 3d', 'empty')
    asm.relative('ff 15', 'puts')
    asm.emit('85 c0')
    asm.relative('0f 88', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit')
    asm.label('text')
    asm.code += b'authored console line\0'
    asm.label('empty')
    asm.code += b'\0'
    return interfaces_fixture(asm.finish(), [('puts', library, 2, module), ('_Exit', 'libc')])


def avplayer_fixture(*, library='libSceAvPlayer', module='libSceAvPlayer', allocation='valid', source=False):
    names = ['sceSysmoduleLoadModule', 'sceSysmoduleUnloadModule', 'sceSysmoduleIsLoaded',
             'sceAvPlayerInit', 'sceAvPlayerClose', 'sceAvPlayerStreamCount', 'sceAvPlayerIsActive',
             'sceAvPlayerStart' if source is True else 'sceAvPlayerAddSource',
             'sceAvPlayerGetStreamInfo', 'sceAvPlayerEnableStream', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + 8 * index for index, name in enumerate(names)})
    asm.labels.update(options=4352, allocate_field=4360, release_field=4368, texture_field=4376,
                      texture_release_field=4384, buffers=4456, allocations=4992, releases=4996,
                      callback_error=5000, ready_events=5004, control=5120, source_state=5136,
                      stream_info=5248, info_tail=5288, event_object=4432, event_callback=4440)
    asm.emit('48 83 ec 08')

    def expected(value=0, *, wide=False):
        asm.emit(('48 3d ' if wide else '3d ') + struct.pack('<I', value & 0xffffffff).hex(' '))
        asm.relative('0f 85', 'fail')

    def call(name, value=0, *, integer=None, pointer=None, handle=False):
        if integer is not None:
            asm.emit('bf ' + struct.pack('<I', integer & 0xffffffff).hex(' '))
        if pointer:
            asm.relative('48 8d 3d', pointer)
        if handle:
            asm.emit('4c 89 e7')
        asm.relative('ff 15', name)
        if value is not None:
            expected(value)

    def store_pointer(field, label):
        asm.relative('48 8d 05', label)
        asm.relative('48 89 05', field)

    def count(field, value):
        asm.relative('83 3d', field, bytes([value]).hex(' '))
        asm.relative('0f 85', 'fail')

    call(names[2], 0x805a1001, integer=0xa5)
    call(names[3], integer=0)  # No module / no arguments must not publish a handle.
    call(names[0], integer=0xa5)
    call(names[0], integer=0xa5)
    call(names[1], integer=0xa5)
    call(names[2], integer=0xa5)
    asm.relative('48 c7 05', 'options', '78 56 34 12')
    for field, label in (('allocate_field', 'allocate'), ('release_field', 'release'),
                         ('texture_field', 'allocate'), ('texture_release_field', 'release')):
        store_pointer(field, label)
    asm.relative('48 c7 05', 'buffers', '02 00 00 00')
    if isinstance(source, str):
        asm.relative('48 c7 05', 'event_object', '78 56 34 12')
        store_pointer('event_callback', 'ready_callback')
    if allocation == 'bad_callback':
        asm.relative('48 c7 05', 'release_field', '34 12 00 00')
    call(names[3], None, pointer='options')
    if allocation != 'valid':
        expected(0, wide=True)
        count('allocations', 0 if allocation == 'bad_callback' else 1)
        count('releases', 1 if allocation == 'unaligned' else 0)
    else:
        asm.emit('48 85 c0')
        asm.relative('0f 84', 'fail')
        asm.emit('49 89 c4')
        asm.relative('4c 39 25', 'control_handle')
        asm.relative('0f 85', 'fail')
        call(names[5], handle=True)
        call(names[6], handle=True)
        call(names[1], 0x80020010, integer=0xa5)
        if source is True:
            asm.emit('4c 89 e7 31 f6')
            call(names[7], 0x806a0002)
            call(names[6], handle=True)
        elif isinstance(source, str):
            asm.emit('4c 89 e7')
            asm.relative('48 8d 35', 'source_path')
            call(names[7], 0 if source == 'metadata' else 0x806a0002)
            call(names[5], 2 if source == 'metadata' else 0, handle=True)
            call(names[6], handle=True)  # Ready is not playing.
            count('ready_events', 1 if source == 'metadata' else 0)
            if source == 'metadata':
                count('source_state', 2)
                asm.relative('48 c7 05', 'info_tail', '73 73 73 73')
                for index in range(2):
                    asm.emit('4c 89 e7 be ' + struct.pack('<I', index).hex(' '))
                    asm.relative('48 8d 15', 'stream_info')
                    call(names[8])
                    asm.relative('48 8d 05', 'stream_info')
                    asm.emit('83 38 00')
                    asm.relative('0f 85', f'audio_info_{index}')
                    for raw in ('83 78 08 40', '83 78 0c 20', '81 78 10 00 00 00 40'):
                        asm.emit(raw)
                        asm.relative('0f 85', 'fail')
                    asm.relative('e9', f'info_done_{index}')
                    asm.label(f'audio_info_{index}')
                    for raw in ('83 38 01', '66 83 78 08 02', '81 78 0c 80 bb 00 00', '83 78 10 00'):
                        asm.emit(raw)
                        asm.relative('0f 85', 'fail')
                    asm.label(f'info_done_{index}')
                    for raw in ('48 81 78 18 ee 02 00 00', '48 83 78 20 00'):
                        asm.emit(raw)
                        asm.relative('0f 85', 'fail')
                    asm.relative('81 3d', 'info_tail', '73 73 73 73')
                    asm.relative('0f 85', 'fail')
                    asm.emit('4c 89 e7 be ' + struct.pack('<I', index).hex(' '))
                    call(names[9])
                asm.emit('4c 89 e7 be 02 00 00 00')
                call(names[9], 0x806a0001)
                asm.emit('4c 89 e7 be 63 00 00 00')
                asm.relative('48 8d 15', 'stream_info')
                call(names[8], 0x806a0001)
                asm.emit('4c 89 e7')
                asm.relative('48 8d 35', 'source_path')
                call(names[7], 0x806a0002)
                count('ready_events', 1)
        call(names[4], handle=True)
        call(names[4], 0x806a0001, handle=True)
        call(names[5], 0x806a0001, handle=True)
        count('allocations', 1)
        count('releases', 1)
        call(names[3], None, pointer='options')
        asm.emit('48 85 c0')
        asm.relative('0f 84', 'fail')
        asm.emit('4c 39 e0')
        asm.relative('0f 84', 'fail')
        asm.emit('49 89 c4')
        call(names[4], handle=True)
        count('allocations', 2)
        count('releases', 2)
    count('callback_error', 0)
    call(names[1], integer=0xa5)
    call(names[2], 0x805a1001, integer=0xa5)
    call(names[1], 0x805a1001, integer=0xa5)
    call(names[3], pointer='options')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')

    asm.label('allocate')
    asm.emit('48 83 ec 08 81 ff 78 56 34 12')
    asm.relative('0f 85', 'callback_fail')
    asm.emit('83 fe 40')
    asm.relative('0f 85', 'callback_fail')
    asm.emit('83 fa 40')
    asm.relative('0f 85', 'callback_fail')
    asm.relative('ff 05', 'allocations')
    asm.emit('bf a5 00 00 00')
    asm.relative('ff 15', names[1])
    asm.emit('3d 10 00 02 80')
    asm.relative('0f 85', 'callback_fail')
    if allocation == 'null':
        asm.emit('31 c0')
    else:
        asm.relative('48 8d 05', 'control')
        if allocation == 'unaligned':
            asm.emit('48 ff c0')
    asm.emit('48 83 c4 08 c3')
    asm.label('release')
    asm.emit('48 83 ec 08 81 ff 78 56 34 12')
    asm.relative('0f 85', 'callback_fail')
    asm.relative('48 8d 05', 'control')
    if allocation == 'unaligned':
        asm.emit('48 ff c0')
    asm.emit('48 39 c6')
    asm.relative('0f 85', 'callback_fail')
    asm.relative('ff 05', 'releases')
    asm.emit('bf a5 00 00 00')
    asm.relative('ff 15', names[1])
    asm.emit('3d 10 00 02 80')
    asm.relative('0f 85', 'callback_fail')
    asm.emit('48 83 c4 08 c3')
    asm.label('callback_fail')
    asm.relative('c7 05', 'callback_error', '01 00 00 00')
    asm.emit('31 c0 48 83 c4 08 c3')
    if isinstance(source, str):
        asm.label('ready_callback')
        asm.emit('48 83 ec 08 81 ff 78 56 34 12')
        asm.relative('0f 85', 'callback_fail')
        asm.emit('83 fe 02')
        asm.relative('0f 85', 'callback_fail')
        asm.emit('85 d2')
        asm.relative('0f 85', 'callback_fail')
        asm.emit('48 85 c9')
        asm.relative('0f 85', 'callback_fail')
        asm.relative('ff 05', 'ready_events')
        asm.relative('48 8b 3d', 'control_handle')
        asm.relative('ff 15', names[5])
        asm.emit('83 f8 02')
        asm.relative('0f 85', 'callback_fail')
        asm.relative('48 8b 3d', 'control_handle')
        asm.relative('ff 15', names[4])
        asm.emit('3d 02 00 6a 80')
        asm.relative('0f 85', 'callback_fail')
        asm.emit('bf a5 00 00 00')
        asm.relative('ff 15', names[1])
        asm.emit('3d 10 00 02 80')
        asm.relative('0f 85', 'callback_fail')
        asm.emit('48 83 c4 08 c3')
        asm.label('source_path')
        asm.code += {'metadata': b'fixture.mp4\0', 'missing': b'/app0/absent.mp4\0',
                     'malformed': b'/app0/not-media.mp4\0'}[source]
    asm.labels['control_handle'] = asm.labels['control'] + 8
    imports = [(name, 'libc' if name == '_Exit' else 'libSceSysmodule' if index < 3 else library,
                2, 'libc' if name == '_Exit' else 'libSceSysmodule' if index < 3 else module)
               for index, name in enumerate(names)]
    return interfaces_fixture(asm.finish(), imports, data_size=2048)


def avplayer_frame_fixture(*, fail_audio_once=False):
    names = ['sceSysmoduleLoadModule', 'sceSysmoduleUnloadModule', 'sceAvPlayerInit', 'sceAvPlayerAddSource',
             'sceAvPlayerEnableStream', 'sceAvPlayerStart', 'sceAvPlayerIsActive', 'sceAvPlayerGetVideoDataEx',
             'sceAvPlayerGetAudioData', 'sceAvPlayerPause', 'sceAvPlayerResume', 'sceAvPlayerStop',
             'sceAvPlayerClose', 'sceKernelUsleep', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(options=4352, mem_allocate=4360, mem_release=4368, tex_allocate=4376, tex_release=4384,
                      event_object=4432, event_callback=4440, buffers=4456, control=5120, handle=5128,
                      alloc_requests=4992, frees=4996, callback_error=5000, ready=5004, play=5008, pause=5012,
                      stop=5016, textures=5020, texture_frees=5024, audio_owned=5032, fail_once=5036,
                      owned=5040, info=6144, video_tail=6248, audio_tail=6184,
                      video0=8192, video1=12288, audio0=16384, audio1=20480)
    asm.emit('48 83 ec 08')

    def call(name, value=0, *, handle=False, boolean=None):
        if handle:
            asm.emit('4c 89 e7')
        asm.relative('ff 15', name)
        if boolean is not None:
            asm.emit('84 c0')
            asm.relative('0f 84' if boolean else '0f 85', 'fail')
        elif value is not None:
            asm.emit('3d ' + struct.pack('<I', value & 0xffffffff).hex(' '))
            asm.relative('0f 85', 'fail')

    def equal(field, value):
        asm.relative('81 3d', field, struct.pack('<I', value).hex(' '))
        asm.relative('0f 85', 'fail')

    def output():
        asm.relative('48 8d 35', 'info')

    def wait(microseconds):
        asm.emit('bf ' + struct.pack('<I', microseconds).hex(' '))
        call(names[13])

    asm.emit('bf a5 00 00 00')
    call(names[0])
    asm.relative('48 c7 05', 'options', '78 56 34 12')
    asm.relative('48 c7 05', 'event_object', '78 56 34 12')
    for field, target in (('mem_allocate', 'allocate'), ('mem_release', 'release'),
                          ('tex_allocate', 'texture_allocate'), ('tex_release', 'texture_release'),
                          ('event_callback', 'callback')):
        asm.relative('48 8d 05', target)
        asm.relative('48 89 05', field)
    asm.relative('48 c7 05', 'buffers', '02 00 00 00')
    if fail_audio_once:
        asm.relative('c7 05', 'fail_once', '01 00 00 00')
    asm.relative('48 8d 3d', 'options')
    call(names[2], None)
    asm.emit('48 85 c0')
    asm.relative('0f 84', 'fail')
    asm.emit('49 89 c4 4c 89 e7')
    asm.relative('48 8d 35', 'path')
    call(names[3])
    equal('ready', 1)
    for index in range(2):
        asm.emit('be ' + struct.pack('<I', index).hex(' '))
        call(names[4], handle=True)
    output()
    call(names[7], handle=True, boolean=False)
    call(names[5], handle=True)
    call(names[6], handle=True, boolean=True)
    asm.relative('48 c7 05', 'video_tail', '73 73 73 73')
    output()
    call(names[7], handle=True, boolean=True)
    equal('video_tail', 0x73737373)
    asm.relative('48 8d 05', 'info')
    for raw in ('83 78 18 40', '83 78 1c 20', '83 78 28 04', '83 78 3c 40',
                '80 78 40 08', '80 78 41 08', '80 78 42 00'):
        asm.emit(raw)
        asm.relative('0f 85', 'fail')
    asm.emit('48 8b 00 48 85 c0')
    asm.relative('0f 84', 'fail')
    # H.264 fixture is limited-range red: Y~81, interleaved U~90/V~240.
    for offset, low, high in ((0, 77, 85), (2048, 86, 94), (2049, 236, 244)):
        asm.emit('80 b8 ' + struct.pack('<I', offset).hex(' ') + ' ' + bytes([low]).hex(' '))
        asm.relative('0f 82', 'fail')
        asm.emit('80 b8 ' + struct.pack('<I', offset).hex(' ') + ' ' + bytes([high]).hex(' '))
        asm.relative('0f 87', 'fail')
    call(names[9], handle=True)
    call(names[6], handle=True, boolean=True)
    asm.relative('48 c7 05', 'info', '73 73 73 73')
    output()
    call(names[8], handle=True, boolean=False)
    equal('info', 0x73737373)
    wait(100000)
    output()
    call(names[7], handle=True, boolean=False)
    call(names[10], handle=True)
    if fail_audio_once:
        output()
        call(names[8], handle=True, boolean=False)
        call(names[6], handle=True, boolean=True)
    asm.emit('45 31 ed')  # r13d counts delivered PCM packets.
    asm.label('audio_loop')
    asm.relative('48 c7 05', 'audio_tail', '73 73 73 73')
    asm.emit('4c 89 e7')
    output()
    call(names[8], None)
    asm.emit('84 c0')
    asm.relative('0f 84', 'audio_end')
    asm.relative('48 8d 05', 'info')
    for raw in ('48 83 38 00', '66 83 78 18 02', '81 78 1c 80 bb 00 00', '81 78 20 00 10 00 00'):
        asm.emit(raw)
        asm.relative('0f 84' if raw == '48 83 38 00' else '0f 85', 'fail')
    equal('audio_tail', 0x73737373)
    asm.emit('41 ff c5 41 83 fd 40')
    asm.relative('0f 87', 'fail')
    asm.relative('e9', 'audio_loop')
    asm.label('audio_end')
    asm.emit('41 83 fd 25')
    asm.relative('0f 85', 'fail')
    call(names[6], handle=True, boolean=True)  # Video EOF has not yet been drained.
    wait(900000)
    output()
    call(names[7], handle=True, boolean=True)
    asm.emit('41 be 64 00 00 00')
    asm.label('poll_end')
    call(names[6], None, handle=True)
    asm.emit('84 c0')
    asm.relative('0f 84', 'ended')
    wait(10000)
    asm.emit('41 ff ce')
    asm.relative('0f 85', 'poll_end')
    asm.relative('e9', 'fail')
    asm.label('ended')
    wait(100000)
    equal('stop', 1)
    equal('pause', 1)
    equal('play', 2)
    equal('callback_error', 0)
    call(names[11], handle=True)
    call(names[12], handle=True)
    equal('owned', 3)
    equal('frees', 3)
    equal('audio_owned', 2)
    equal('textures', 2)
    equal('texture_frees', 2)
    equal('callback_error', 0)
    output()
    call(names[8], handle=True, boolean=False)
    asm.emit('bf a5 00 00 00')
    call(names[1])
    asm.emit('bf 07 00 00 00')
    call('_Exit', None)
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    call('_Exit', None)

    def callback_check(raw):
        asm.emit(raw)
        asm.relative('0f 85', 'callback_fail')

    def return_pointer(label):
        asm.relative('48 8d 05', label)
        asm.emit('48 83 c4 08 c3')

    asm.label('allocate')
    asm.emit('48 83 ec 08')
    callback_check('81 ff 78 56 34 12')
    callback_check('83 fe 40')
    asm.relative('f0 ff 05', 'alloc_requests')
    asm.emit('83 fa 40')
    asm.relative('0f 85', 'allocate_audio')
    asm.relative('f0 ff 05', 'owned')
    return_pointer('control')
    asm.label('allocate_audio')
    callback_check('81 fa 00 10 00 00')
    asm.relative('83 3d', 'fail_once', '00')
    asm.relative('0f 84', 'audio_success')
    asm.relative('c7 05', 'fail_once', '00 00 00 00')
    asm.emit('31 c0 48 83 c4 08 c3')
    asm.label('audio_success')
    asm.relative('f0 ff 05', 'owned')
    asm.relative('f0 ff 05', 'audio_owned')
    asm.relative('83 3d', 'audio_owned', '01')
    asm.relative('0f 85', 'second_audio')
    return_pointer('audio0')
    asm.label('second_audio')
    return_pointer('audio1')
    asm.label('release')
    asm.emit('48 83 ec 08')
    callback_check('81 ff 78 56 34 12')
    asm.relative('f0 ff 05', 'frees')
    asm.emit('48 83 c4 08 c3')
    asm.label('texture_allocate')
    asm.emit('48 83 ec 08')
    callback_check('81 ff 78 56 34 12')
    callback_check('81 fe 00 01 00 00')
    callback_check('81 fa 00 0c 00 00')
    asm.relative('f0 ff 05', 'textures')
    asm.relative('83 3d', 'textures', '01')
    asm.relative('0f 85', 'second_texture')
    return_pointer('video0')
    asm.label('second_texture')
    return_pointer('video1')
    asm.label('texture_release')
    asm.emit('48 83 ec 08')
    callback_check('81 ff 78 56 34 12')
    asm.relative('f0 ff 05', 'texture_frees')
    asm.emit('48 83 c4 08 c3')
    asm.label('callback')
    asm.emit('48 83 ec 08')
    callback_check('81 ff 78 56 34 12')
    callback_check('85 d2')
    callback_check('48 85 c9')
    for value, label in ((1, 'stop_event'), (2, 'ready_event'), (3, 'play_event'), (4, 'pause_event')):
        asm.emit('83 fe ' + bytes([value]).hex(' '))
        asm.relative('0f 84', label)
    asm.relative('e9', 'callback_fail')
    for label, counter in (('ready_event', 'ready'), ('play_event', 'play'), ('pause_event', 'pause')):
        asm.label(label)
        asm.relative('f0 ff 05', counter)
        asm.emit('48 83 c4 08 c3')
    asm.label('stop_event')
    asm.relative('f0 ff 05', 'stop')
    asm.relative('48 8b 3d', 'handle')
    asm.relative('ff 15', names[12])
    callback_check('3d 02 00 6a 80')
    asm.emit('48 83 c4 08 c3')
    asm.label('callback_fail')
    asm.relative('c7 05', 'callback_error', '01 00 00 00')
    asm.emit('31 c0 48 83 c4 08 c3')
    asm.label('path')
    asm.code += b'/app0/fixture.mp4\0'
    imports = [(name, 'libc' if name == '_Exit' else 'libkernel' if name == 'sceKernelUsleep' else
                'libSceSysmodule' if index < 2 else 'libSceAvPlayer') for index, name in enumerate(names)]
    return interfaces_fixture(asm.finish(), imports, data_size=24576)


def text_conversion_fixture(*, library='libc', module='libc'):
    names = ['mbsrtowcs', 'wcsrtombs', 'wcslen', 'wmemcpy', 'wmemmove', 'wmemcmp', '__error', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + 8 * index for index, name in enumerate(names)})
    asm.labels.update(source=4352, text=4384, state=4416, wide=4480, wide_tail=4488,
                      bytes=4528, bytes_tail=4536, invalid=4576, invalid_tail=4577,
                      copied=4640, shifted=4642, copied_tail=4648)

    def pointer(label, target):
        asm.relative('48 8d 05', target)
        asm.relative('48 89 05', label)

    def store(label, value):
        asm.emit('48 b8 ' + struct.pack('<Q', value).hex(' '))
        asm.relative('48 89 05', label)

    def equal(label, value):
        asm.emit('48 b8 ' + struct.pack('<Q', value).hex(' '))
        asm.relative('48 39 05', label)
        asm.relative('0f 85', 'fail')

    def call(name, expected, *, wide=True):
        asm.relative('ff 15', name)
        if expected is not None:
            asm.emit(('48 83 f8 ' if wide else '83 f8 ') + struct.pack('<b', expected).hex())
            asm.relative('0f 85', 'fail')

    def convert(name, output, capacity):
        if output:
            asm.relative('48 8d 3d', output)
        else:
            asm.emit('31 ff')
        asm.relative('48 8d 35', 'source')
        asm.emit('ba ' + struct.pack('<I', capacity).hex(' '))
        asm.relative('48 8d 0d', 'state')

    asm.emit('48 83 ec 08')
    asm.relative('ff 15', '__error')
    asm.emit('49 89 c4 c7 00 49 00 00 00')
    store('text', int.from_bytes(b'A\xc3\xa9\xe7\xb5\x86\0\0', 'little'))
    store('wide', 0xa5a5a5a5a5a5a5a5); store('wide_tail', 0xa5a5a5a5a5a5a5a5)
    store('bytes', 0x4949494949494949); store('bytes_tail', 0x4949494949494949)
    pointer('source', 'text')
    convert('mbsrtowcs', None, 0); call('mbsrtowcs', 3)
    asm.relative('48 8d 05', 'text'); asm.relative('48 39 05', 'source'); asm.relative('0f 85', 'fail')
    convert('mbsrtowcs', 'wide', 4); call('mbsrtowcs', 3)
    equal('wide', 0x00007d4600e90041); equal('wide_tail', 0xa5a5a5a5a5a5a5a5); equal('source', 0)
    asm.emit('41 83 3c 24 49'); asm.relative('0f 85', 'fail')
    asm.relative('48 8d 3d', 'wide'); call('wcslen', 3)
    pointer('source', 'wide')
    convert('wcsrtombs', None, 0); call('wcsrtombs', 6)
    asm.relative('48 8d 05', 'wide'); asm.relative('48 39 05', 'source'); asm.relative('0f 85', 'fail')
    convert('wcsrtombs', 'bytes', 7); call('wcsrtombs', 6)
    equal('bytes', int.from_bytes(b'A\xc3\xa9\xe7\xb5\x86\0I', 'little')); equal('bytes_tail', 0x4949494949494949); equal('source', 0)
    store('copied', 0xa5a5a5a5a5a5a5a5); store('copied_tail', 0x7373737373737373)
    asm.relative('48 8d 3d', 'copied'); asm.relative('48 8d 35', 'wide'); asm.emit('ba 03 00 00 00'); call('wmemcpy', None)
    asm.relative('48 8d 0d', 'copied'); asm.emit('48 39 c8'); asm.relative('0f 85', 'fail')
    equal('copied', 0xa5a57d4600e90041)
    asm.relative('48 8d 3d', 'shifted'); asm.relative('48 8d 35', 'copied'); asm.emit('ba 03 00 00 00'); call('wmemmove', None)
    asm.relative('48 8d 0d', 'shifted'); asm.emit('48 39 c8'); asm.relative('0f 85', 'fail')
    equal('copied', 0x7d4600e900410041); equal('copied_tail', 0x7373737373737373)
    asm.relative('48 8d 3d', 'wide'); asm.relative('48 8d 35', 'copied'); asm.emit('ba 03 00 00 00'); call('wmemcmp', 1, wide=False)
    asm.relative('48 8d 3d', 'copied'); asm.relative('48 8d 35', 'wide'); asm.emit('ba 03 00 00 00'); call('wmemcmp', -1, wide=False)
    store('invalid', int.from_bytes(b'B\xff\0\0\0\0\0\0', 'little')); pointer('source', 'invalid')
    convert('mbsrtowcs', 'wide', 4); call('mbsrtowcs', -1)
    asm.relative('48 8d 05', 'invalid_tail'); asm.relative('48 39 05', 'source'); asm.relative('0f 85', 'fail')
    asm.emit('41 83 3c 24 56'); asm.relative('0f 85', 'fail')
    equal('wide', 0x00007d4600e90042); equal('state', 0)
    asm.emit('bf 07 00 00 00'); asm.relative('ff 15', '_Exit'); asm.emit('0f 0b')
    asm.label('fail'); asm.emit('bf 63 00 00 00'); asm.relative('ff 15', '_Exit'); asm.emit('0f 0b')
    return interfaces_fixture(asm.finish(), [(name, library, 2, module) if name in names[:6]
                                             else (name, 'libkernel' if name == '__error' else 'libc') for name in names], data_size=768)


def wide_format_fixture(*, library='libc', module='libc', unsupported=False):
    names = ['swprintf', 'vswprintf', '__error', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(output=4352, numeric_format=4512, short_format=4560, va=4608, va_registers=4624, registers=4656)

    def store(label, value):
        asm.emit('48 b8 ' + struct.pack('<Q', value).hex(' '))
        asm.relative('48 89 05', label)

    def store_bytes(label, value):
        base = asm.labels[label]
        for index in range(0, len(value), 8):
            part = f'{label}_{index}'; asm.labels[part] = base + index
            store(part, int.from_bytes(value[index:index + 8].ljust(8, b'\0'), 'little'))

    def equal(label, value):
        asm.emit('48 b8 ' + struct.pack('<Q', value).hex(' '))
        asm.relative('48 39 05', label); asm.relative('0f 85', 'fail')

    def call(name, expected):
        asm.relative('ff 15', name)
        if expected is not None:
            asm.emit('83 f8 ' + struct.pack('<b', expected).hex())
            asm.relative('0f 85', 'fail')

    def inputs(format_label, capacity):
        asm.relative('48 8d 3d', 'output')
        asm.emit('be ' + struct.pack('<I', capacity).hex(' '))
        asm.relative('48 8d 15', format_label)

    asm.emit('48 83 ec 18')
    call('__error', None); asm.emit('49 89 c4 c7 00 49 00 00 00')
    store_bytes('output', bytes.fromhex('73 73') * 64)
    store_bytes('numeric_format', (('%s' if unsupported else '\u7d46=%ld/%.1f') + '\0').encode('utf-16-le'))
    store_bytes('short_format', ('%d%d%d%d\0').encode('utf-16-le'))
    inputs('numeric_format', 64)
    asm.emit('48 b9 01 00 00 00 01 00 00 00 48 b8 ' + struct.pack('<d', 1.25).hex(' ') + ' 66 48 0f 6e c0 b0 01')
    call('swprintf', 16)
    expected = '\u7d46=4294967297/1.2'.encode('utf-16-le')
    for index in range(0, len(expected), 8):
        label = f'expected_{index}'; asm.labels[label] = 4352 + index
        equal(label, int.from_bytes(expected[index:index + 8], 'little'))
    asm.labels['output_tail'] = 4384; equal('output_tail', 0x7373737373730000)
    store('output', 0x7373737373737373)
    inputs('short_format', 3)
    asm.emit('b9 01 00 00 00 41 b8 02 00 00 00 41 b9 03 00 00 00 48 c7 04 24 04 00 00 00 31 c0')
    call('swprintf', -1); equal('output', 0x7373000000320031)
    asm.emit('41 83 3c 24 49'); asm.relative('0f 85', 'fail')
    store('va', 48 << 32)
    asm.relative('48 8d 05', 'registers'); asm.relative('48 89 05', 'va_registers')
    for index, value in enumerate((5, 6, 7, 8)):
        label = f'register_{index}'; asm.labels[label] = 4656 + index * 8; store(label, value)
    inputs('short_format', 64); asm.relative('48 8d 0d', 'va'); call('vswprintf', 4)
    equal('output', int.from_bytes('5678'.encode('utf-16-le'), 'little')); equal('va', 48 << 32)
    asm.emit('bf 07 00 00 00'); asm.relative('ff 15', '_Exit'); asm.emit('0f 0b')
    asm.label('fail'); asm.emit('bf 63 00 00 00'); asm.relative('ff 15', '_Exit'); asm.emit('0f 0b')
    return interfaces_fixture(asm.finish(), [(name, library, 2, module) if name in names[:2]
                                             else (name, 'libkernel' if name == '__error' else 'libc') for name in names], data_size=1024)


def jump_fixture(value=37, *, signal_mask=False, library='libc'):
    asm = SyntheticCode()
    asm.labels.update(setjmp=4112, longjmp=4120, exit=4128, buffer=4400, jump_tail=4488, stack=4500, status=4512, mxcsr=4520, x87=4528, base_pointer=4544)
    asm.emit('48 83 ec 08')
    asm.relative('48 89 25', 'stack')
    asm.relative('48 89 2d', 'base_pointer')
    asm.relative('c6 05', 'jump_tail', '73')
    asm.relative('c7 05', 'mxcsr', '80 3f 00 00')
    asm.relative('66 c7 05', 'x87', '7f 07')
    asm.relative('0f ae 15', 'mxcsr')
    asm.relative('d9 2d', 'x87')
    saved = [('48 bb', '48 39 d8', 0x1122334455667788), ('49 bc', '4c 39 e0', 0x2233445566778899),
             ('49 bd', '4c 39 e8', 0x33445566778899aa), ('49 be', '4c 39 f0', 0x445566778899aabb),
             ('49 bf', '4c 39 f8', 0x5566778899aabbcc)]
    for mov, _, expected in saved:
        asm.emit(mov + ' ' + struct.pack('<Q', expected).hex(' '))
    asm.relative('48 8d 3d', 'buffer')
    asm.relative('ff 15', 'setjmp')
    asm.emit('85 c0')
    asm.relative('0f 85', 'resumed')
    if signal_mask:
        asm.relative('c7 05', 'buffer_mask', '01 00 00 00')
    asm.relative('c7 05', 'mxcsr', '81 5f 00 00')
    asm.relative('66 c7 05', 'x87', '7f 0b')
    asm.relative('0f ae 15', 'mxcsr')
    asm.relative('d9 2d', 'x87')
    asm.emit('31 db 31 ed 45 31 e4 45 31 ed 45 31 f6 45 31 ff')
    asm.relative('48 8d 3d', 'buffer')
    asm.emit('be ' + struct.pack('<I', value & 0xffffffff).hex(' '))
    asm.relative('ff 15', 'longjmp')
    asm.relative('e9', 'fail')
    asm.label('resumed')
    asm.emit('3d ' + struct.pack('<I', (value or 1) & 0xffffffff).hex(' '))
    asm.relative('0f 85', 'fail')
    asm.relative('48 3b 25', 'stack')
    asm.relative('0f 85', 'fail')
    asm.relative('48 3b 2d', 'base_pointer')
    asm.relative('0f 85', 'fail')
    asm.relative('80 3d', 'jump_tail', '73')
    asm.relative('0f 85', 'fail')
    for _, cmp, expected in saved:
        asm.emit('48 b8 ' + struct.pack('<Q', expected).hex(' ') + ' ' + cmp)
        asm.relative('0f 85', 'fail')
    asm.relative('0f ae 1d', 'status')
    asm.relative('81 3d', 'status', '81 3f 00 00')
    asm.relative('0f 85', 'fail')
    asm.relative('d9 3d', 'x87')
    asm.relative('66 81 3d', 'x87', '7f 07')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit')
    asm.labels['buffer_mask'] = asm.labels['buffer'] + 72
    return interfaces_fixture(asm.finish(), [('setjmp', library), ('longjmp', library), ('_Exit', 'libc')], data_size=600)


def formatting_fixture(*, variadic=False, unsupported=False, library='libc', unbounded=False):
    name = ('sprintf' if variadic else 'vsprintf') if unbounded else ('snprintf' if variadic else 'vsnprintf')
    asm = SyntheticCode()
    asm.labels.update({name: 4112, 'memcmp': 4120, '_Exit': 4128})
    asm.labels.update(va=4400, fp_offset=4404, overflow=4408, register_base=4416,
                      registers=4480, second=4488, first_fp=4528, stack=4680,
                      buffer=4700, sentinel=4701, format=4800, string=4864, expected=4900)
    asm.emit('48 83 ec 08')
    asm.relative('c7 05', 'va', '00 00 00 00')
    asm.relative('c7 05', 'fp_offset', '30 00 00 00')
    for label, target in (('register_base', 'registers'), ('overflow', 'stack'), ('registers', 'string')):
        asm.relative('48 8d 05', target)
        asm.relative('48 89 05', label)
    asm.emit('48 b8 ' + struct.pack('<q', -4294967297).hex(' '))
    asm.relative('48 89 05', 'second')
    asm.emit('48 b8 ' + struct.pack('<d', 1.25).hex(' '))
    asm.relative('48 89 05', 'first_fp')
    asm.relative('c6 05', 'sentinel', '7f')
    for size in ((64,) if unbounded else (1, 64)):
        asm.relative('48 8d 3d', 'buffer')
        if unbounded:
            asm.relative('48 8d 35', 'format')
        else:
            asm.emit('be ' + struct.pack('<I', size).hex(' '))
            asm.relative('48 8d 15', 'format')
        if variadic:
            asm.relative('48 8d 15' if unbounded else '48 8d 0d', 'string')
            asm.emit(('48 b9 ' if unbounded else '49 b8 ') + struct.pack('<q', -4294967297).hex(' '))
            asm.relative('f3 0f 7e 05', 'first_fp')
            asm.emit('b0 01')
        else:
            asm.relative('48 8d 15' if unbounded else '48 8d 0d', 'va')
        asm.relative('ff 15', name)
        asm.emit('83 f8 12')
        asm.relative('0f 85', 'fail')
        if size == 1:
            for label, value in (('buffer', 0), ('sentinel', 127)):
                asm.relative('80 3d', label, f'{value:02x}')
                asm.relative('0f 85', 'fail')
    asm.relative('48 8d 3d', 'buffer')
    asm.relative('48 8d 35', 'expected')
    asm.emit('ba 13 00 00 00')
    asm.relative('ff 15', 'memcmp')
    asm.emit('85 c0')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.emit('0f 0b')
    code = asm.finish()
    packed = interfaces_fixture(code, [(name, library), ('memcmp', 'libc'), ('_Exit', 'libc')], data_size=1024)
    start = 96 + 32 + len(code) + 32
    for offset, text in ((4800, '%n' if unsupported else '%s:%ld:%.1f'), (4864, 'ok'), (4900, 'ok:-4294967297:1.2')):
        value = text.encode() + b'\0'
        packed[start + offset - 4096:start + offset - 4096 + len(value)] = value
    return packed


def audio_fixture(*, library='libSceAudioOut', format_code=1):
    names = ['sceAudioOutInit', 'sceAudioOutOpen', 'sceAudioOutSetVolume', 'sceAudioOutOutput', 'sceAudioOutClose', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(volume=4400, samples=4416)
    asm.emit('48 83 ec 08')

    def call(name, expected=0):
        asm.relative('ff 15', name)
        asm.emit('3d ' + struct.pack('<I', expected).hex(' '))
        asm.relative('0f 85', 'fail')

    call(names[0])
    call(names[0], 0x8026000e)
    asm.emit('bf ff 00 00 00 31 f6 31 d2 b9 00 01 00 00 41 b8 80 bb 00 00 41 b9 ' + struct.pack('<I', format_code).hex(' '))
    asm.relative('ff 15', names[1])
    asm.emit('85 c0')
    asm.relative('0f 8e', 'fail')
    asm.emit('41 89 c4')
    asm.relative('c7 05', 'volume', '00 40 00 00')
    asm.emit('44 89 e7 be 03 00 00 00')
    asm.relative('48 8d 15', 'volume')
    call(names[2])
    asm.emit('44 89 e7')
    asm.relative('48 8d 35', 'samples')
    channels = 1 if (format_code & 255) in (0, 3) else 2 if (format_code & 255) in (1, 4) else 8
    call(names[3], 256 * channels)
    asm.emit('44 89 e7 31 f6')
    call(names[3])
    asm.emit('44 89 e7')
    call(names[4])
    asm.emit('44 89 e7')
    call(names[4], 0x80260001)
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.emit('0f 0b')
    return interfaces_fixture(asm.finish(), [(name, 'libc' if name == '_Exit' else library) for name in names], data_size=512 + 256 * channels * 4)


def content_fixture(*, empty_addons=False, built_in=False, module='libSceAppContentUtil'):
    names = ['sceSysmoduleLoadModule', 'sceAppContentAppParamGetInt', 'sceAppContentInitialize',
             'sceAppContentGetAddcontInfoList', 'sceAppContentAddcontMount', 'sceAppContentGetEntitlementKey',
             'sceAppContentAddcontUnmount', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + i * 8 for i, name in enumerate(names)})
    asm.labels.update(init=4400, boot=4440, boot_sentinel=4480, count=4496, parameter=4500,
                      entry=4512, point=4544, key=4560, key_sentinel=4576)
    asm.emit('48 83 ec 08 bf b4 00 00 00')

    def call(name, expected=0):
        asm.relative('ff 15', name)
        asm.emit('3d ' + struct.pack('<I', expected & 0xffffffff).hex(' '))
        asm.relative('0f 85', 'fail')

    call(names[0])
    asm.emit('bf 01 00 00 00')
    asm.relative('48 8d 35', 'parameter')
    call(names[1])
    asm.relative('81 3d', 'parameter', 'd2 04 00 00')
    asm.relative('0f 85', 'fail')
    asm.relative('48 c7 05', 'boot_sentinel', '49 00 00 00')
    asm.relative('48 8d 3d', 'init')
    asm.relative('48 8d 35', 'boot')
    call(names[2])
    asm.relative('48 83 3d', 'boot_sentinel', '49')
    asm.relative('0f 85', 'fail')
    asm.emit('31 ff')
    asm.relative('48 8d 35', 'parameter')
    call(names[1])
    asm.relative('83 3d', 'parameter', '03')
    asm.relative('0f 85', 'fail')
    asm.emit('31 ff 31 f6 31 d2')
    asm.relative('48 8d 0d', 'count')
    call(names[3])
    asm.relative('83 3d', 'count', '00' if empty_addons else '02')
    asm.relative('0f 85', 'fail')
    if not empty_addons:
        asm.emit('31 ff')
        asm.relative('48 8d 35', 'entry')
        asm.emit('ba 01 00 00 00')
        asm.relative('48 8d 0d', 'count')
        call(names[3])
        asm.relative('48 8d 05', 'entry')
        asm.emit('83 78 14 ' + ('00' if built_in else '04'))
        asm.relative('0f 85', 'fail')
        asm.emit('31 ff')
        asm.relative('48 8d 35', 'entry')
        asm.relative('48 8d 15', 'point')
        call(names[4], 0x80d90005 if built_in else 0)
        asm.relative('48 c7 05', 'key_sentinel', '49 00 00 00')
        asm.emit('31 ff')
        asm.relative('48 8d 35', 'entry')
        asm.relative('48 8d 15', 'key')
        call(names[5])
        asm.relative('48 83 3d', 'key_sentinel', '49')
        asm.relative('0f 85', 'fail')
        if built_in:
            # Both fields must contain the exact, terminated capability names;
            # no secret or game data is part of this authored fixture.
            for digit in (1, 2):
                asm.relative('48 8d 05', 'key')
                for offset in (0, 8):
                    expected = f'KETSUI_PS4_DLC{digit}'.encode() + b'\0'
                    asm.emit('48 b9 ' + expected[offset:offset + 8].hex(' '))
                    asm.emit('48 39 48 ' + f'{offset:02x}')
                    asm.relative('0f 85', 'fail')
                if digit == 1:
                    asm.relative('48 8d 05', 'entry')
                    asm.emit('c6 40 0f 32')  # Select the second stable 16-byte label.
                    asm.emit('31 ff')
                    asm.relative('48 8d 35', 'entry')
                    asm.relative('48 8d 15', 'key')
                    call(names[5])
            asm.relative('48 8d 05', 'entry')
            asm.emit('c6 40 0f 39')
            asm.emit('31 ff')
            asm.relative('48 8d 35', 'entry')
            asm.relative('48 8d 15', 'key')
            call(names[5], 0x80d90007)  # Unknown entries must still fail.
            asm.relative('48 8d 05', 'key')
            for offset in (0, 8):
                asm.emit('48 b9 ' + (b'KETSUI_PS4_DLC2\0')[offset:offset + 8].hex(' '))
                asm.emit('48 39 48 ' + f'{offset:02x}')
                asm.relative('0f 85', 'fail')
            asm.relative('48 83 3d', 'key_sentinel', '49')
            asm.relative('0f 85', 'fail')
            asm.relative('48 8d 3d', 'point')
            call(names[6], 0x80d90005)  # No fictitious mount was registered.
        else:
            asm.relative('48 8d 3d', 'point')
            call(names[6])
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.emit('0f 0b')
    imports = [(name, 'libc' if name == '_Exit' else 'libSceSysmodule' if name == names[0] else 'libSceAppContent',
                2, 'libc' if name == '_Exit' else 'libSceSysmodule' if name == names[0] else module) for name in names]
    return interfaces_fixture(asm.finish(), imports, data_size=512)


def thread_fixture(*, invalid_callback=False, library='libkernel'):
    names = ['scePthreadAttrInit', 'scePthreadAttrSetstacksize', 'scePthreadCreate', 'scePthreadAttrDestroy',
             'scePthreadJoin', '__error', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(attribute=4400, thread=4408, result=4416, sentinel=4424, argument=4432)
    asm.emit('48 83 ec 08')
    asm.relative('ff 15', '__error')
    asm.emit('49 89 c4 c7 00 d2 04 00 00')
    asm.relative('48 c7 05', 'sentinel', '49 00 00 00')

    def call(name, expected=0):
        asm.relative('ff 15', name)
        asm.emit('3d ' + struct.pack('<I', expected).hex(' '))
        asm.relative('0f 85', 'fail')

    asm.relative('48 8d 3d', 'attribute')
    call(names[0])
    asm.relative('48 8d 3d', 'attribute')
    asm.emit('be 00 00 02 00')
    call(names[1])
    asm.relative('48 8d 3d', 'thread')
    asm.relative('48 8d 35', 'attribute')
    asm.relative('48 8d 15', 'argument' if invalid_callback else 'entry')
    asm.relative('48 8d 0d', 'argument')
    asm.relative('4c 8d 05', 'name')
    call(names[2], 0x80020016 if invalid_callback else 0)
    asm.relative('48 8d 3d', 'attribute')
    call(names[3])
    if not invalid_callback:
        asm.relative('48 8b 3d', 'thread')
        asm.relative('48 8d 35', 'result')
        call(names[4])
        asm.relative('48 8b 05', 'result')
        asm.emit('48 b9 10 32 54 76 98 ba dc fe')
        asm.emit('48 39 c8')
        asm.relative('0f 85', 'fail')
    asm.emit('41 81 3c 24 d2 04 00 00')
    asm.relative('0f 85', 'fail')
    asm.relative('48 83 3d', 'sentinel', '49')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('fail')
    asm.emit('48 83 e4 f0 bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.emit('0f 0b')
    asm.label('entry')
    asm.emit('53 48 89 fb')
    asm.relative('48 8d 05', 'argument')
    asm.emit('48 39 c3')
    asm.relative('0f 85', 'fail')
    asm.relative('ff 15', '__error')
    asm.emit('83 38 00')
    asm.relative('0f 85', 'fail')
    asm.emit('c7 00 e1 10 00 00')
    asm.relative('48 8b 3d', 'thread')
    asm.emit('31 f6')
    call(names[4], 0x8002000b)
    asm.emit('48 b8 10 32 54 76 98 ba dc fe 5b c3')
    asm.label('name')
    asm.code += b'Ketsui-Synthetic\0'
    imports = [(name, 'libc' if name == '_Exit' else 'libkernel' if name == '__error' else library) for name in names]
    return interfaces_fixture(asm.finish(), imports, data_size=512)


def ngs2_fixture(*, invalid_callback=False, unknown_control=False, library='libSceNgs2', sampler=False, lock_control=False):
    names = ['sceSysmoduleLoadModule', 'sceNgs2SystemCreateWithAllocator', 'sceNgs2RackCreateWithAllocator',
             'sceNgs2RackGetVoiceHandle', 'sceNgs2VoiceControl', 'sceNgs2RackDestroy', 'sceNgs2SystemDestroy',
             'sceSysmoduleUnloadModule', 'malloc', 'free', '_Exit', 'sceNgs2RackLock', 'sceNgs2RackUnlock', 'sceNgs2SystemRender',
             'sceNgs2VoiceGetStateFlags']
    if lock_control:
        names += ['scePthreadMutexLock', 'scePthreadMutexUnlock', 'scePthreadCreate', 'scePthreadJoin',
                  'sceKernelUsleep', 'scePthreadMutexDestroy']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(allocator=4400, release_slot=4408, userdata=4416, system=4440, rack=4448, voice=4456,
                      sentinel=4464, param=4480, param_channels=4488, param_flags=4492, alloc_count=4500, free_count=4504)
    asm.emit('48 83 ec 08 bf 0b 00 00 00')

    def call(name, expected=0):
        asm.relative('ff 15', name)
        asm.emit('3d ' + struct.pack('<I', expected).hex(' '))
        asm.relative('0f 85', 'fail')

    call(names[0])
    asm.relative('48 8d 05', 'allocator' if invalid_callback else 'allocate_callback')
    asm.relative('48 89 05', 'allocator')
    asm.relative('48 8d 05', 'free_callback')
    asm.relative('48 89 05', 'release_slot')
    asm.relative('48 c7 05', 'userdata', '39 30 00 00')
    asm.relative('48 c7 05', 'sentinel', '63 00 00 00')
    asm.emit('31 ff')
    asm.relative('48 8d 35', 'allocator')
    asm.relative('48 8d 15', 'system')
    call(names[1], 0x804a020a if invalid_callback else 0)
    if not invalid_callback:
        asm.relative('48 8b 3d', 'system')
        asm.emit('be 00 30 00 00 31 d2')
        asm.relative('48 8d 0d', 'allocator')
        asm.relative('4c 8d 05', 'rack')
        call(names[2])
        asm.relative('48 8b 3d', 'rack')
        asm.emit('31 f6')
        asm.relative('48 8d 15', 'voice')
        call(names[3])
        for _ in range(2):
            asm.relative('48 8b 3d', 'rack')
            call('sceNgs2RackLock')
        asm.relative('48 c7 05', 'param', '10 00 00 00')
        asm.relative('c7 05', 'param_channels', '02 00 00 00')
        asm.relative('c7 05', 'param_flags', '00 00 00 00')
        # Header id occupies the upper half of this little-endian qword.
        asm.relative('48 8d 05', 'param')
        asm.emit('c7 40 04 ' + ('77 77 00 00' if unknown_control else '00 00 00 30'))
        asm.relative('48 8b 3d', 'voice')
        asm.relative('48 8d 35', 'param')
        call(names[4])
        if not unknown_control:
            asm.labels.update(state_flags=4544, state_tail=4548)
            asm.relative('48 c7 05', 'state_flags', '49 00 00 00')
            asm.relative('c7 05', 'state_tail', '5b 00 00 00')
            asm.relative('48 8b 3d', 'voice')
            asm.relative('48 8d 35', 'state_flags')
            call('sceNgs2VoiceGetStateFlags')
            for label, expected in (('state_flags', '01'), ('state_tail', '5b')):
                asm.relative('83 3d', label, expected)
                asm.relative('0f 85', 'fail')
            asm.emit('48 b8 0c 00 00 00 06 00 00 00')
            asm.relative('48 89 05', 'param')
            asm.relative('c7 05', 'param_channels', '00 00 00 00')
            asm.relative('48 8b 3d', 'voice')
            asm.relative('48 8d 35', 'param')
            call(names[4])
        if lock_control:
            asm.labels.update(lock_thread=4560, lock_result=4568, lock_ready=4576, lock_progress=4580,
                              lock_timeout=4584, stream_mutex=4592)
            asm.relative('48 8d 3d', 'lock_thread')
            asm.emit('31 f6')
            asm.relative('48 8d 15', 'lock_worker')
            asm.emit('31 c9 45 31 c0')
            call('scePthreadCreate')
            asm.emit('bb c8 00 00 00')  # bounded 200 x 1 ms; always unlock before joining
            asm.label('lock_poll')
            asm.relative('83 3d', 'lock_progress', '01')
            asm.relative('0f 84', 'lock_polled')
            asm.emit('bf e8 03 00 00')
            call('sceKernelUsleep')
            asm.emit('ff cb')
            asm.relative('0f 85', 'lock_poll')
            asm.relative('c7 05', 'lock_timeout', '01 00 00 00')
            asm.label('lock_polled')
        if sampler:
            asm.labels.update(sample_option=4608, sample_rack=4800, sample_voice=4808, pcm=4816, block=4832,
                              setup=4864, blocks=4912, patch=4944, render=4976, matrix=5008, matrix_levels=5032, output=5120, output_tail=7168)

            def word(address, value):
                label = f'word_{address}'
                asm.labels[label] = address
                asm.relative('c7 05', label, struct.pack('<I', value).hex(' '))

            def pointer(address, target):
                label = f'pointer_{address}'
                asm.labels[label] = address
                asm.relative('48 8d 05', target)
                asm.relative('48 89 05', label)

            def header(target, value):
                asm.emit('48 b8 ' + struct.pack('<Q', value).hex(' '))
                asm.relative('48 89 05', target)

            for address, value in ((4608, 168), (4636, 512), (4640, 1), (4648, 1), (4652, 8), (4736, 2), (4744, 4)):
                word(address, value)
            asm.relative('48 8b 3d', 'system')
            asm.emit('be 00 10 00 00')
            asm.relative('48 8d 15', 'sample_option')
            asm.relative('48 8d 0d', 'allocator')
            asm.relative('4c 8d 05', 'sample_rack')
            call(names[2])
            asm.relative('48 8b 3d', 'sample_rack')
            asm.emit('31 f6')
            asm.relative('48 8d 15', 'sample_voice')
            call(names[3])
            header('setup', 0x1000000000000028)
            for address, value in ((4872, 18), (4876, 1), (4880, 48000), (4896, 1)):
                word(address, value)

            def control(param):
                asm.relative('48 8b 3d', 'sample_voice')
                asm.relative('48 8d 35', param)
                call(names[4])

            control('setup')
            header('patch', 0x500000018)
            asm.relative('48 8b 05', 'voice')
            asm.labels['destination'] = 4960
            asm.relative('48 89 05', 'destination')
            control('patch')
            header('matrix', 0x100000018)
            word(5020, 2); pointer(5024, 'matrix_levels'); word(5032, 0x3f800000); word(5036, 0x3f800000)
            control('matrix')
            header('pcm', 0x00007fff40008000)
            word(4836, 8); word(4848, 4); word(4852, 0xa5a5a5a5)
            header('blocks', 0x1000000100000020)
            pointer(4920, 'pcm'); word(4932, 1); pointer(4936, 'block')
            control('blocks'); control('param')
            pointer(4976, 'output'); word(4984, 2048); word(4992, 24); word(4996, 2); word(7168, 73)
            asm.relative('48 8b 3d', 'system')
            asm.relative('48 8d 35', 'render')
            asm.emit('ba 01 00 00 00')
            call('sceNgs2SystemRender')
            for address, expected in ((5120, 0xbf800000), (5124, 0xbf800000), (5128, 0x3f000000), (5136, 0x3f7ffe00), (5144, 0), (7168, 73)):
                label = f'check_{address}'; asm.labels[label] = address
                asm.relative('81 3d', label, struct.pack('<I', expected).hex(' '))
                asm.relative('0f 85', 'fail')
            asm.relative('48 8b 3d', 'sample_rack')
            asm.emit('31 f6'); call(names[5])
        for _ in range(2):
            asm.relative('48 8b 3d', 'rack')
            call('sceNgs2RackUnlock')
        if lock_control:
            asm.relative('48 8b 3d', 'lock_thread')
            asm.relative('48 8d 35', 'lock_result')
            call('scePthreadJoin')
            asm.relative('48 8d 3d', 'stream_mutex')
            call('scePthreadMutexDestroy')
            for label, value in (('lock_timeout', 0), ('lock_ready', 1), ('lock_progress', 1), ('lock_result', 0)):
                asm.relative('83 3d', label, f'{value:02x}')
                asm.relative('0f 85', 'fail')
        asm.relative('48 8b 3d', 'rack')
        call('sceNgs2RackUnlock', 0x804a030f)
        asm.relative('48 8b 3d', 'rack')
        asm.emit('31 f6')
        call(names[5])
        asm.relative('48 8b 3d', 'system')
        asm.emit('31 f6')
        call(names[6])
        for counter in ('alloc_count', 'free_count'):
            asm.relative('83 3d', counter, '03' if sampler else '02')
            asm.relative('0f 85', 'fail')
    asm.relative('48 83 3d', 'sentinel', '63')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 0b 00 00 00')
    call(names[7])
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('fail')
    asm.emit('48 83 e4 f0 bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.emit('0f 0b')
    asm.label('allocate_callback')
    asm.emit('53 48 89 fb 48 81 7f 38 39 30 00 00')
    asm.relative('0f 85', 'fail')
    asm.emit('48 8b 7f 08')
    asm.relative('ff 15', 'malloc')
    asm.emit('48 89 03')
    asm.relative('ff 05', 'alloc_count')
    asm.emit('31 c0 5b c3')
    asm.label('free_callback')
    asm.emit('48 83 ec 08 48 81 7f 38 39 30 00 00')
    asm.relative('0f 85', 'fail')
    asm.emit('48 8b 3f')
    asm.relative('ff 15', 'free')
    asm.relative('ff 05', 'free_count')
    asm.emit('31 c0 48 83 c4 08 c3')
    if lock_control:
        asm.label('lock_worker')
        asm.emit('48 83 ec 08')
        asm.relative('48 8d 3d', 'stream_mutex')
        call('scePthreadMutexLock')
        asm.relative('c7 05', 'lock_ready', '01 00 00 00')
        asm.relative('48 8b 3d', 'voice')
        asm.relative('48 8d 35', 'param')
        call('sceNgs2VoiceControl')
        asm.relative('c7 05', 'lock_progress', '01 00 00 00')
        asm.relative('48 8d 3d', 'stream_mutex')
        call('scePthreadMutexUnlock')
        asm.emit('31 c0 48 83 c4 08 c3')
    imports = [(name, 'libc' if name in ('malloc', 'free', '_Exit') else
                'libSceSysmodule' if name in (names[0], names[7]) else
                'libkernel' if name.startswith('scePthread') or name == 'sceKernelUsleep' else library) for name in names]
    return interfaces_fixture(asm.finish(), imports, data_size=4096 if sampler else 512)


def aligned_heap_fixture():
    names = ['memalign', 'free', '__error', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.emit('48 83 ec 08')
    for alignment, mask in ((128, 127), (70, 127), (4, 31), (0, 31)):
        asm.emit('bf ' + struct.pack('<I', alignment).hex(' ') + ' be 23 00 00 00')
        asm.relative('ff 15', names[0])
        asm.emit('48 85 c0')
        asm.relative('0f 84', 'fail')
        asm.emit('48 a9 ' + struct.pack('<I', mask).hex(' '))
        asm.relative('0f 85', 'fail')
        asm.emit('c6 00 55 c6 40 22 aa 48 89 c7')
        asm.relative('ff 15', names[1])
    asm.emit('48 bf ff ff ff ff ff ff ff ff be 23 00 00 00')
    asm.relative('ff 15', names[0])
    asm.emit('48 85 c0')
    asm.relative('0f 85', 'fail')
    asm.relative('ff 15', names[2])
    asm.emit('83 38 0c')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.emit('0f 0b')
    return interfaces_fixture(asm.finish(), [(name, 'libkernel' if name == '__error' else 'libc') for name in names])


def printf_fixture():
    asm = SyntheticCode()
    asm.labels.update(printf=4112, _Exit=4120, format=4400, string=4480, floating=4500)
    asm.emit('48 83 ec 08')
    asm.relative('48 8d 3d', 'format')
    asm.relative('48 8d 35', 'string')
    asm.emit('48 ba ' + struct.pack('<q', -4294967297).hex(' '))
    asm.relative('f3 0f 7e 05', 'floating')
    asm.emit('b0 01')
    asm.relative('ff 15', 'printf')
    asm.emit('83 f8 13')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.emit('0f 0b')
    code = asm.finish()
    packed = interfaces_fixture(code, [('printf', 'libc'), ('_Exit', 'libc')], data_size=512)
    start = 96 + 32 + len(code) + 32
    for offset, value in ((4400, b'%s:%ld:%.1f\n\0'), (4480, b'ok\0'), (4500, struct.pack('<d', 1.25))):
        packed[start + offset - 4096:start + offset - 4096 + len(value)] = value
    return packed


def module_image_fixture(missing=False, data_fault=False):
    asm = SyntheticCode()
    asm.labels.update(cxa=4112, guard=4120, count=4400, argc=4408, argv=4416, pointer=4424)
    asm.emit('48 83 ec 08')
    asm.relative('48 89 3d', 'argc')
    asm.relative('48 89 35', 'argv')
    asm.relative('83 05', 'count', '01')
    asm.relative('48 8d 3d', 'destructor')
    asm.relative('48 8d 35', 'count')
    asm.relative('48 8d 15', 'count')
    asm.relative('ff 15', 'cxa')
    asm.emit('b8 11 00 00 00 48 83 c4 08 c3')
    asm.label('value')
    asm.relative('48 8b 05', 'pointer')
    asm.emit('8b 00')
    asm.relative('03 05', 'count')
    asm.emit('c3')
    asm.label('canary')
    asm.relative('48 8b 05', 'guard')
    asm.emit('48 8b 00 c3')
    asm.label('destructor')
    asm.emit('83 07 0a c3')
    code = asm.finish()
    result = bytearray(struct.pack('<8s7Q32s', b'KTMODULE', 8192, 0, 2, 3, 2, 0, 0, bytes(32)))
    result += struct.pack('<4Q', 0, 0, 1, 3)
    result += struct.pack('<4Q', 0, 4096, 5, len(code)) + code
    data = bytearray(512)
    struct.pack_into('<I', data, 4432 - 4096, 42)
    result += struct.pack('<4Q', 4096, 4096, 6, len(data)) + data
    for name, library, kind in (('ModuleMissing' if missing else '__cxa_atexit', 'libc', 2),
                                ('ModuleDataMissing' if data_fault else '__stack_chk_guard', 'libkernel', 1)):
        for field in (nid(name) + '#A#A', name, library, library):
            encoded = field.encode()
            result += struct.pack('<I', len(encoded)) + encoded
        result += struct.pack('<I', kind)
    for name, label, kind, size in (('TestValue', 'value', 2, 1), ('TestCanary', 'canary', 2, 1), ('TestState', 'count', 1, 24)):
        for field in (nid(name) + '#A#A', name, 'libFixture', 'libFixture'):
            encoded = field.encode()
            result += struct.pack('<I', len(encoded)) + encoded
        result += struct.pack('<IIQQ', kind, 1, asm.labels[label], size)
    for relocation in ((4112, 1, 0, 0), (4120, 1, 1, 0), (4424, 0, 4432, 0)):
        result += struct.pack('<3Qq', *relocation)
    return result


def module_main_fixture(path='/app0/fixture.prx', *, late_imports=False):
    names = ['sceKernelLoadStartModule', 'sceKernelDlsym', '__cxa_finalize', '_Exit', '__stack_chk_guard']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(path=4400, result=4448, output=4456, value_name=4480, state_name=4496, canary_name=4512,
                      unknown=4528, argument=4544)
    asm.emit('48 83 ec 08')
    if late_imports:
        names += ['TestValue', 'TestState']
        asm.labels.update(TestValue=4152, TestState=4160)
        asm.relative('4c 8b 3d', 'TestValue')

    def load():
        asm.relative('48 8d 3d', 'path')
        asm.emit('be 04 00 00 00')
        asm.relative('48 8d 15', 'argument')
        asm.emit('31 c9 45 31 c0')
        asm.relative('4c 8d 0d', 'result')
        asm.relative('ff 15', names[0])

    def call(name, expected=0):
        asm.relative('ff 15', name)
        asm.emit('3d ' + struct.pack('<I', expected).hex(' '))
        asm.relative('0f 85', 'fail')

    def symbol(label):
        asm.emit('89 df')
        asm.relative('48 8d 35', label)
        asm.relative('48 8d 15', 'output')
        call(names[1])

    load()
    asm.emit('85 c0')
    asm.relative('0f 8e', 'fail')
    asm.emit('89 c3')
    if late_imports:
        asm.emit('41 ff d7 83 f8 2b')
        asm.relative('0f 85', 'fail')
        call('TestValue', 43)
        asm.relative('48 8b 05', 'TestState')
        asm.emit('83 38 01')
        asm.relative('0f 85', 'fail')
    asm.relative('83 3d', 'result', '11')
    asm.relative('0f 85', 'fail')
    load()
    asm.emit('39 d8')
    asm.relative('0f 85', 'fail')
    symbol('value_name')
    asm.relative('4c 8b 35', 'output')
    asm.emit('41 ff d6 83 f8 2b')
    asm.relative('0f 85', 'fail')
    symbol('state_name')
    asm.relative('4c 8b 2d', 'output')
    asm.emit('41 83 7d 00 01')
    asm.relative('0f 85', 'fail')
    asm.emit('49 83 7d 08 04')
    asm.relative('0f 85', 'fail')
    asm.relative('48 8d 05', 'argument')
    asm.emit('49 39 45 10')
    asm.relative('0f 85', 'fail')
    symbol('canary_name')
    asm.relative('ff 15', 'output')
    asm.relative('4c 8b 15', '__stack_chk_guard')
    asm.emit('49 3b 02')
    asm.relative('0f 85', 'fail')
    asm.emit('4c 89 ef')
    asm.relative('ff 15', names[2])
    asm.emit('41 ff d6 83 f8 35')
    asm.relative('0f 85', 'fail')
    asm.emit('89 df')
    asm.relative('48 8d 35', 'unknown')
    asm.relative('48 8d 15', 'output')
    call(names[1], 0x80020003)
    asm.relative('48 83 3d', 'output', '00')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.emit('0f 0b')
    imports = [(name, 'libFixture' if name in ('TestValue', 'TestState') else 'libc' if name in ('__cxa_finalize', '_Exit') else 'libkernel',
                1 if name in ('__stack_chk_guard', 'TestState') else 2) for name in names]
    result = interfaces_fixture(asm.finish(), imports, data_size=512)
    data_start = 96 + 32 + len(asm.finish()) + 32
    for offset, text in ((4400, path), (4480, 'TestValue'), (4496, 'TestState'), (4512, 'TestCanary'), (4528, 'Missing')):
        payload = text.encode() + b'\0'
        result[data_start + offset - 4096:data_start + offset - 4096 + len(payload)] = payload
    return result


def direct_memory_fixture(action="normal", library="libkernel"):
    names = ["sceKernelGetDirectMemorySize", "sceKernelAllocateDirectMemory",
             "sceKernelMapDirectMemory", "sceKernelMapNamedDirectMemory",
             "sceKernelMunmap", "sceKernelReleaseDirectMemory", "sceKernelMprotect", "_Exit"]
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(physical=4400, first=4408, second=4416, name=4424)
    asm.emit("48 83 ec 18")  # SysV alignment; stack slot for the named map's 7th argument

    def call(name, expected=0):
        asm.relative("ff 15", name)
        asm.emit("3d " + struct.pack("<I", expected).hex(" "))
        asm.relative("0f 85", "fail")

    asm.relative("ff 15", names[0])
    asm.emit("48 89 c6 31 ff ba 00 00 01 00 b9 00 00 20 00 41 b8 03 00 00 00")
    asm.relative("4c 8d 0d", "physical")
    call(names[1])

    def map_arguments(destination, *, size=0x10000, offset=0, protection=0x33):
        asm.relative("48 8d 3d", destination)
        asm.emit("be " + struct.pack("<I", size).hex(" "))
        asm.emit("ba " + struct.pack("<I", protection).hex(" ") + " 31 c9")
        asm.relative("4c 8b 05", "physical")
        if offset:
            asm.emit("49 81 c0 " + struct.pack("<I", offset).hex(" "))
        asm.emit("41 b9 00 00 20 00")

    map_arguments("first")
    call(names[2])
    asm.relative("4c 8b 25", "first")  # r12 points at the first view
    asm.emit("41 f7 c4 ff ff 1f 00")  # alignment
    asm.relative("0f 85", "fail")
    asm.emit("41 80 3c 24 00")
    asm.relative("0f 85", "fail")
    asm.emit("41 c6 84 24 00 40 00 00 5a")
    map_arguments("second", size=0x4000, offset=0x4000,
                  protection=0x31 if action == "readonly" else 0x30 if action == "noaccess" else 0x33)
    asm.relative("48 8d 05", "name")
    asm.emit("48 89 04 24")
    call(names[3])
    asm.relative("4c 8b 2d", "second")
    if action == "noaccess":
        asm.emit("41 8a 45 00")
    else:
        asm.emit("41 80 7d 00 5a")
        asm.relative("0f 85", "fail")
        asm.emit("41 c6 45 01 9b")
    if action in ("readonly", "noaccess"):
        asm.emit("0f 0b")
    elif action == "release_access":
        asm.relative("48 8b 3d", "physical")
        asm.emit("be 00 00 01 00")
        call(names[5])
        asm.emit("41 8a 04 24 0f 0b")
    elif action == "partial_release":
        asm.relative("48 8b 3d", "physical")
        asm.emit("be 00 40 00 00")
        call(names[5])
        asm.emit("0f 0b")
    elif action == "partial_unmap":
        asm.emit("4c 89 e7 be 00 40 00 00")
        call(names[4])
        asm.emit("0f 0b")
    else:
        asm.emit("41 80 bc 24 01 40 00 00 9b")
        asm.relative("0f 85", "fail")
        asm.emit("4c 89 ef be 00 40 00 00 ba 31 00 00 00")
        call(names[6])
        asm.emit("41 80 7d 01 9b")
        asm.relative("0f 85", "fail")
        asm.emit("4c 89 ef be 00 40 00 00")
        call(names[4])
        asm.emit("41 80 bc 24 01 40 00 00 9b")
        asm.relative("0f 85", "fail")
        asm.relative("48 8b 3d", "physical")
        asm.emit("be 00 00 01 00")
        call(names[5])
        asm.emit("bf 07 00 00 00")
        asm.relative("ff 15", "_Exit")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [(name, "libc" if name == "_Exit" else library)
                                            for name in names], data_size=512)


def mspace_fixture(action="normal"):
    names = ["sceKernelAllocateDirectMemory", "sceKernelMapDirectMemory", "sceKernelReleaseDirectMemory",
             "sceKernelMunmap", "sceKernelMprotect", "sceLibcMspaceCreate", "sceLibcMspaceMemalign",
             "sceLibcMspaceFree", "sceLibcMspaceDestroy", "__error", "_Exit"]
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(physical=4400, view=4408, alias=4416, name=4424)
    asm.emit("48 83 ec 08 31 ff be 00 00 01 00 ba 00 00 01 00 b9 00 40 00 00 45 31 c0")
    asm.relative("4c 8d 0d", "physical")

    def call(name):
        asm.relative("ff 15", name)
        asm.emit("85 c0")
        asm.relative("0f 85", "fail")

    def pointer_call(name, zero=False):
        asm.relative("ff 15", name)
        asm.emit("48 85 c0")
        asm.relative("0f 85" if zero else "0f 84", "fail")

    def map_view(label):
        asm.relative("48 8d 3d", label)
        asm.emit("be 00 00 01 00 ba 33 00 00 00 31 c9")
        asm.relative("4c 8b 05", "physical")
        asm.emit("41 b9 00 40 00 00")
        call(names[1])

    def create(label, zero=False):
        asm.relative("48 8d 3d", "name")
        asm.relative("48 8b 35", label)
        asm.emit("ba 00 00 01 00 31 c9")
        pointer_call(names[5], zero=zero)

    def allocate(alignment, size, zero=False):
        asm.emit("4c 89 e7 be " + struct.pack("<I", alignment).hex(" ") + " ba " + struct.pack("<I", size).hex(" "))
        pointer_call(names[6], zero=zero)

    call(names[0])
    map_view("view")
    create("view")
    asm.emit("49 89 c4")  # r12 = opaque arena handle
    map_view("alias")
    create("alias", zero=True)  # same physical range, different virtual mapping
    allocate(4096, 31)
    asm.emit("49 89 c5 a9 ff 0f 00 00")
    asm.relative("0f 85", "fail")
    asm.emit("41 c6 45 00 5a")
    allocate(16, 0)
    asm.emit("49 89 c6 4d 39 ee")
    asm.relative("0f 84", "fail")
    allocate(16, 0x10000, zero=True)
    asm.relative("ff 15", "__error")
    asm.emit("83 38 0c")
    asm.relative("0f 85", "fail")
    allocate(3, 16)
    asm.emit("a9 1f 00 00 00")
    asm.relative("0f 85", "fail")
    asm.emit("48 89 c6 4c 89 e7")
    asm.relative("ff 15", names[7])
    asm.emit("41 80 7d 00 5a")
    asm.relative("0f 85", "fail")
    if action == "release_pinned":
        asm.relative("48 8b 3d", "physical")
        asm.emit("be 00 00 01 00")
        call(names[2])
        asm.emit("0f 0b")
    elif action in ("unmap_pinned", "protect_pinned"):
        asm.relative("48 8b 3d", "view")
        asm.emit("be 00 00 01 00 ba 31 00 00 00")
        call(names[4] if action == "protect_pinned" else names[3])
        asm.emit("0f 0b")
    else:
        asm.emit("4c 89 e7 4c 89 ee")
        asm.relative("ff 15", names[7])
        if action == "double_free":
            asm.emit("4c 89 e7 4c 89 ee")
            asm.relative("ff 15", names[7])
            asm.emit("0f 0b")
        asm.emit("4c 89 e7 4c 89 f6")
        asm.relative("ff 15", names[7])
        allocate(16, 0x10000)
        asm.relative("48 3b 05", "view")
        asm.relative("0f 85", "fail")
        asm.emit("c6 80 ff ff 00 00 a7 4c 89 e7")
        call(names[8])
        if action == "stale_handle":
            allocate(16, 16)
            asm.emit("0f 0b")
        asm.relative("48 8b 3d", "physical")
        asm.emit("be 00 00 01 00")
        call(names[2])
        asm.emit("bf 07 00 00 00")
        asm.relative("ff 15", "_Exit")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [(name, "libkernel" if name.startswith("sceKernel") or name == "__error" else "libc")
                                            for name in names], data_size=512)


def video_fixture(action="normal", library="libSceVideoOut"):
    names = ["sceVideoOutOpen", "sceVideoOutClose", "sceVideoOutGetResolutionStatus", "_Exit"]
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(resolution=4400)
    asm.emit("48 83 ec 08 bf ff 00 00 00 31 f6 31 d2 31 c9")
    if action == "parameter":
        asm.relative("48 8d 0d", "resolution")
    asm.relative("ff 15", names[0])
    asm.emit("85 c0")
    asm.relative("0f 8e", "fail")
    asm.emit("89 c3 89 df")
    asm.relative("48 8d 35", "resolution")
    asm.relative("ff 15", names[2])
    asm.emit("85 c0")
    asm.relative("0f 85", "fail")
    for label, offset, value in (("width", 0, 1280), ("height", 4, 720), ("refresh", 16, 0), ("tail", 44, 0)):
        asm.labels[label] = 4400 + offset
        asm.relative("81 3d", label, struct.pack("<I", value).hex(" "))
        asm.relative("0f 85", "fail")
    asm.emit("89 df")
    asm.relative("ff 15", names[1])
    asm.emit("85 c0")
    asm.relative("0f 85", "fail")
    asm.emit("89 df")
    asm.relative("ff 15", names[1])
    asm.emit("3d 0b 00 29 80")
    asm.relative("0f 85", "fail")
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [(name, "libc" if name == "_Exit" else library) for name in names], data_size=512)


def libc_memory_fixture(library="libc", zero=False):
    names = ["memset", "memcpy", "memmove", "memcmp", "strlen", "strcmp", "strncmp", "_Exit"]
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(buffer=4400, shifted=4401, copy=4432)
    asm.emit("48 83 ec 08")
    if zero:
        for name in names[:4]:
            asm.emit("31 ff 31 f6 31 d2")
            asm.relative("ff 15", name)
            asm.emit("48 85 c0")
            asm.relative("0f 85", "fail")
    else:
        asm.relative("48 8d 3d", "buffer")
        asm.emit("be ab 01 00 00 ba 10 00 00 00")
        asm.relative("ff 15", "memset")
        asm.relative("48 8d 0d", "buffer")
        asm.emit("48 39 c8")
        asm.relative("0f 85", "fail")
        asm.relative("81 3d", "buffer", "ab ab ab ab")
        asm.relative("0f 85", "fail")
        asm.relative("48 8d 3d", "copy")
        asm.relative("48 8d 35", "buffer")
        asm.emit("ba 10 00 00 00")
        asm.relative("ff 15", "memcpy")
        asm.relative("48 8d 0d", "copy")
        asm.emit("48 39 c8")
        asm.relative("0f 85", "fail")
        asm.relative("48 8d 3d", "copy")
        asm.relative("48 8d 35", "buffer")
        asm.emit("ba 10 00 00 00")
        asm.relative("ff 15", "memcmp")
        asm.emit("85 c0")
        asm.relative("0f 85", "fail")
        asm.relative("c6 05", "copy", "01")
        asm.relative("48 8d 3d", "buffer")
        asm.relative("48 8d 35", "copy")
        asm.emit("ba 10 00 00 00")
        asm.relative("ff 15", "memcmp")
        asm.emit("85 c0")
        asm.relative("0f 8e", "fail")  # 0xab > 1 when compared as unsigned bytes
        asm.relative("c7 05", "buffer", "01 02 03 04")
        for destination, source, compare in (("shifted", "buffer", "shifted"), ("buffer", "shifted", "buffer")):
            asm.relative("48 8d 3d", destination)
            asm.relative("48 8d 35", source)
            asm.emit("ba 0f 00 00 00")
            asm.relative("ff 15", "memmove")
            asm.relative("81 3d", compare, "01 02 03 04")
            asm.relative("0f 85", "fail")
        asm.relative("48 8d 3d", "buffer")
        asm.relative("ff 15", "strlen")
        asm.emit("48 83 f8 10")
        asm.relative("0f 85", "fail")
        asm.relative("48 8d 3d", "buffer")
        asm.relative("48 8d 35", "copy")
        asm.relative("ff 15", "strcmp")
        asm.emit("85 c0")
        asm.relative("0f 89", "fail")
        asm.relative("48 8d 3d", "buffer")
        asm.relative("48 8d 35", "copy")
        asm.emit("ba 01 00 00 00")
        asm.relative("ff 15", "strncmp")
        asm.emit("85 c0")
        asm.relative("0f 85", "fail")
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [(name, "libc" if name == "_Exit" else library) for name in names], data_size=512)


def cxx_guard_fixture(action="normal", library="libc"):
    names = ["__cxa_guard_acquire", "__cxa_guard_release", "__cxa_guard_abort", "_Exit"]
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(guard=4400, flag=4400)
    asm.emit("48 83 ec 08")

    def acquire(expected=1):
        asm.relative("48 8d 3d", "guard")
        asm.relative("ff 15", names[0])
        asm.emit("83 f8 " + f"{expected:02x}")
        asm.relative("0f 85", "fail")

    if action == "not_owned":
        asm.relative("48 8d 3d", "guard")
        asm.relative("ff 15", names[1])
    elif action == "misaligned":
        asm.labels["guard"] = 4401
        acquire()
    else:
        acquire()
        if action == "recursive":
            acquire()
        else:
            asm.relative("80 3d", "flag", "00")
            asm.relative("0f 85", "fail")
            asm.relative("48 8d 3d", "guard")
            asm.relative("ff 15", names[2])
            acquire()
            asm.relative("48 8d 3d", "guard")
            asm.relative("ff 15", names[1])
            asm.relative("80 3d", "flag", "01")
            asm.relative("0f 85", "fail")
            acquire(0)
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [(name, "libc" if name == "_Exit" else library) for name in names], data_size=512)


def profile_fixture(library="libkernel"):
    asm = SyntheticCode()
    asm.labels.update(query=4112, immediate=4120)
    asm.emit("48 83 ec 08")
    asm.relative("ff 15", "query")
    asm.emit("85 c0")
    asm.relative("0f 85", "fail")
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "immediate")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "immediate")
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [("sceKernelIsNeoMode", library), ("_Exit", "libc")])


def video_attribute_fixture(null=False):
    names = ["sceVideoOutSetBufferAttribute", "memset", "_Exit"]
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(attribute=4400, sentinel=4440)
    asm.emit("48 83 ec 18")
    asm.relative("48 8d 3d", "attribute")
    asm.emit("be cc 00 00 00 ba 28 00 00 00")
    asm.relative("ff 15", "memset")
    asm.relative("c7 05", "sentinel", "a7 a7 a7 a7")
    if null:
        asm.emit("31 ff")
    else:
        asm.relative("48 8d 3d", "attribute")
    asm.emit("be 00 22 00 80 ba 01 00 00 00 31 c9 41 b8 80 02 00 00 41 b9 e0 01 00 00 c7 04 24 a0 02 00 00")
    asm.relative("ff 15", names[0])
    for index, value in enumerate((0x80002200, 1, 0, 640, 480, 672, 0, 0, 0, 0)):
        label = f"field_{index}"
        asm.labels[label] = 4400 + index * 4
        asm.relative("81 3d", label, struct.pack("<I", value).hex(" "))
        asm.relative("0f 85", "fail")
    asm.relative("81 3d", "sentinel", "a7 a7 a7 a7")
    asm.relative("0f 85", "fail")
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [(name, "libSceVideoOut" if name == names[0] else "libc") for name in names], data_size=512)


def graphics_registration_fixture(protect_pinned=False):
    names = ["sceVideoOutOpen", "sceKernelAllocateDirectMemory", "sceKernelMapDirectMemory",
             "sceVideoOutSetBufferAttribute", "sceVideoOutRegisterBuffers", "sceVideoOutUnregisterBuffers",
             "sceVideoOutClose", "sceKernelReleaseDirectMemory", "sceKernelMprotect", "_Exit"]
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(physical=4400, view=4408, addresses=4416, address1=4424, attribute=4480)
    asm.emit("48 83 ec 18 bf ff 00 00 00 31 f6 31 d2 31 c9")
    asm.relative("ff 15", names[0])
    asm.emit("85 c0")
    asm.relative("0f 8e", "fail")
    asm.emit("41 89 c4 31 ff be 00 00 02 00 ba 00 00 02 00 b9 00 00 20 00 41 b8 03 00 00 00")
    asm.relative("4c 8d 0d", "physical")

    def call(name):
        asm.relative("ff 15", name)
        asm.emit("85 c0")
        asm.relative("0f 85", "fail")

    call(names[1])
    asm.relative("48 8d 3d", "view")
    asm.emit("be 00 00 02 00 ba 33 00 00 00 31 c9")
    asm.relative("4c 8b 05", "physical")
    asm.emit("41 b9 00 00 20 00")
    call(names[2])
    asm.relative("48 8b 05", "view")
    asm.relative("48 89 05", "addresses")
    asm.emit("48 05 00 80 00 00")
    asm.relative("48 89 05", "address1")
    asm.relative("48 8d 3d", "attribute")
    asm.emit("be 00 00 00 80 31 d2 31 c9 41 b8 80 00 00 00 41 b9 40 00 00 00 c7 04 24 80 00 00 00")
    asm.relative("ff 15", names[3])
    asm.emit("44 89 e7 31 f6")
    asm.relative("48 8d 15", "addresses")
    asm.emit("b9 02 00 00 00")
    asm.relative("4c 8d 05", "attribute")
    asm.relative("ff 15", names[4])
    asm.emit("85 c0")
    asm.relative("0f 88", "fail")
    asm.emit("89 c3")
    if protect_pinned:
        asm.relative("48 8b 3d", "view")
        asm.emit("be 00 00 02 00 ba 03 00 00 00")
        call(names[8])
        asm.emit("0f 0b")
    else:
        asm.emit("44 89 e7 89 de")
        call(names[5])
        asm.emit("44 89 e7")
        call(names[6])
        asm.relative("48 8b 3d", "physical")
        asm.emit("be 00 00 02 00")
        call(names[7])
        asm.emit("bf 07 00 00 00")
        asm.relative("ff 15", "_Exit")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [(name, "libkernel" if name.startswith("sceKernel")
        else "libc" if name == "_Exit" else "libSceVideoOut") for name in names], data_size=512)


def video_clock_queue_fixture(close_first=False):
    names = ["sceVideoOutOpen", "sceVideoOutSetFlipRate", "sceKernelCreateEqueue",
             "sceVideoOutAddVblankEvent", "sceKernelWaitEqueue", "sceVideoOutGetEventId",
             "sceVideoOutGetEventCount", "sceKernelGetEventUserData", "sceVideoOutGetEventData",
             "sceVideoOutDeleteVblankEvent", "sceKernelDeleteEqueue", "sceVideoOutClose", "_Exit"]
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(queue=4400, event=4440, available=4472, payload=4480, timeout=4496, name=4504)
    asm.emit("48 83 ec 08 bf ff 00 00 00 31 f6 31 d2 31 c9")
    asm.relative("ff 15", names[0])
    asm.emit("85 c0")
    asm.relative("0f 8e", "fail")
    asm.emit("41 89 c4 44 89 e7 31 f6")

    def call(name, expected=0):
        asm.relative("ff 15", name)
        asm.emit("3d " + struct.pack("<I", expected).hex(" "))
        asm.relative("0f 85", "fail")

    call(names[1])
    asm.relative("48 8d 3d", "queue")
    asm.relative("48 8d 35", "name")
    call(names[2])
    asm.relative("48 8b 3d", "queue")
    asm.emit("44 89 e6 ba 34 12 00 00")
    call(names[3])
    asm.relative("c7 05", "timeout", "40 42 0f 00")
    asm.relative("48 8b 3d", "queue")
    asm.relative("48 8d 35", "event")
    asm.emit("ba 01 00 00 00")
    asm.relative("48 8d 0d", "available")
    asm.relative("4c 8d 05", "timeout")
    call(names[4])
    asm.relative("83 3d", "available", "01")
    asm.relative("0f 85", "fail")
    asm.relative("48 8d 3d", "event")
    call(names[5], 1)
    asm.relative("48 8d 3d", "event")
    asm.relative("ff 15", names[6])
    asm.emit("83 f8 01")
    asm.relative("0f 8c", "fail")
    asm.emit("83 f8 0f")
    asm.relative("0f 8f", "fail")
    asm.relative("48 8d 3d", "event")
    asm.relative("ff 15", names[7])
    asm.emit("48 3d 34 12 00 00")
    asm.relative("0f 85", "fail")
    asm.relative("48 8d 3d", "event")
    asm.relative("48 8d 35", "payload")
    call(names[8])
    asm.relative("48 83 3d", "payload", "01")
    asm.relative("0f 82", "fail")
    if close_first:
        asm.emit("44 89 e7")
        call(names[11])
    else:
        asm.relative("48 8b 3d", "queue")
        asm.emit("44 89 e6")
        call(names[9])
    asm.relative("c7 05", "timeout", "00 00 00 00")
    asm.relative("48 8b 3d", "queue")
    asm.relative("48 8d 35", "event")
    asm.emit("ba 01 00 00 00")
    asm.relative("48 8d 0d", "available")
    asm.relative("4c 8d 05", "timeout")
    call(names[4], 0x8002003C)
    asm.relative("48 8b 3d", "queue")
    call(names[10])
    if not close_first:
        asm.emit("44 89 e7")
        call(names[11])
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [(name, "libkernel" if name.startswith("sceKernel") else
        "libc" if name == "_Exit" else "libSceVideoOut") for name in names], data_size=512)


def video_flip_payload_fixture():
    names = ["sceVideoOutGetEventData", "sceVideoOutGetEventId", "sceVideoOutGetEventCount", "_Exit"]
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(event=4400, filter=4408, data=4416, payload=4440)
    asm.emit("48 83 ec 08")
    asm.relative("c7 05", "event", "06 00 00 00")
    asm.relative("66 c7 05", "filter", "f3 ff")
    asm.emit("48 b8 00 30 fe ff ff ff ff ff")
    asm.relative("48 89 05", "data")
    asm.relative("48 8d 3d", "event")
    asm.relative("48 8d 35", "payload")
    asm.relative("ff 15", names[0])
    asm.emit("85 c0")
    asm.relative("0f 85", "fail")
    asm.relative("48 83 3d", "payload", "fe")
    asm.relative("0f 85", "fail")
    for name, expected in ((names[1], 0), (names[2], 3)):
        asm.relative("48 8d 3d", "event")
        asm.relative("ff 15", name)
        asm.emit("83 f8 " + f"{expected:02x}")
        asm.relative("0f 85", "fail")
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [(name, "libc" if name == "_Exit" else "libSceVideoOut") for name in names], data_size=512)


def errno_fixture():
    asm = SyntheticCode()
    asm.labels.update(malloc_slot=4112, free_slot=4120, error_slot=4128, immediate_slot=4136)
    asm.emit("48 83 ec 08")
    asm.relative("ff 15", "error_slot")
    asm.emit("49 89 c4 c7 00 25 00 00 00 bf 20 00 00 00")
    asm.relative("ff 15", "malloc_slot")
    asm.emit("48 85 c0")
    asm.relative("0f 84", "fail")
    asm.emit("41 83 3c 24 25")
    asm.relative("0f 85", "fail")
    asm.emit("48 89 c7")
    asm.relative("ff 15", "free_slot")
    asm.emit("41 83 3c 24 25")
    asm.relative("0f 85", "fail")
    asm.emit("48 c7 c7 ff ff ff ff")
    asm.relative("ff 15", "malloc_slot")
    asm.emit("48 85 c0")
    asm.relative("0f 85", "fail")
    asm.emit("41 83 3c 24 0c")
    asm.relative("0f 85", "fail")
    asm.relative("ff 15", "error_slot")
    asm.emit("4c 39 e0")
    asm.relative("0f 85", "fail")
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "immediate_slot")
    asm.emit("0f 0b")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "immediate_slot")
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [("malloc", "libc"), ("free", "libc"),
                                            ("__error", "libkernel"), ("_Exit", "libc")])


def mutex_fixture():
    asm = SyntheticCode()
    asm.labels.update(attribute=4096, mutex=4104)
    names = ["scePthreadMutexattrInit", "scePthreadMutexattrSettype", "scePthreadMutexattrDestroy",
             "scePthreadMutexInit", "scePthreadMutexLock", "scePthreadMutexTrylock",
             "scePthreadMutexUnlock", "scePthreadMutexDestroy", "_Exit"]
    for index, name in enumerate(names):
        asm.labels[name] = 4112 + index * 8

    def call(name, expected=0):
        asm.relative("ff 15", name)
        asm.emit("3d " + struct.pack("<I", expected).hex(" "))
        asm.relative("0f 85", "fail")

    asm.emit("48 83 ec 08")
    asm.relative("48 8d 3d", "attribute")
    call(names[0])
    asm.relative("48 8d 3d", "attribute")
    asm.emit("be 02 00 00 00")
    call(names[1])
    asm.relative("48 8d 3d", "mutex")
    asm.relative("48 8d 35", "attribute")
    asm.emit("31 d2")
    call(names[3])
    asm.relative("48 8d 3d", "attribute")
    call(names[2])
    for name, expected in ((names[4], 0), (names[5], 0), (names[7], 0x80020010),
                           (names[6], 0), (names[6], 0), (names[6], 0x80020001),
                           (names[7], 0), (names[4], 0x80020016)):
        asm.relative("48 8d 3d", "mutex")
        call(name, expected)
    asm.emit("31 ff")
    call(names[0], 0x80020016)
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [(name, "libc" if name == "_Exit" else "libkernel")
                                            for name in names])


def dialog_fixture(unknown_module=False):
    names = ["sceSysmoduleLoadModule", "sceSysmoduleIsLoaded", "sceSysmoduleUnloadModule",
             "sceCommonDialogInitialize", "sceMsgDialogInitialize", "sceMsgDialogGetStatus",
             "sceMsgDialogClose", "sceMsgDialogGetResult", "sceMsgDialogOpen", "sceMsgDialogTerminate",
             "sceErrorDialogInitialize", "sceErrorDialogGetStatus", "sceErrorDialogOpen",
             "sceErrorDialogTerminate", "_Exit"]
    asm = SyntheticCode()
    for index, name in enumerate(names):
        asm.labels[name] = 4112 + index * 8
    asm.labels.update(param=4400, param_size=4448)

    def call(name, expected=0, integer=None, pointer=None):
        if integer is not None:
            asm.emit("bf " + struct.pack("<I", integer).hex(" "))
        if pointer:
            asm.relative("48 8d 3d", pointer)
        asm.relative("ff 15", name)
        asm.emit("3d " + struct.pack("<I", expected).hex(" "))
        asm.relative("0f 85", "fail")

    asm.emit("48 83 ec 08")
    if unknown_module:
        call(names[0], integer=0x7777)
        asm.relative("e9", "fail")
    else:
        call(names[1], 0x805A1001, integer=0xA4)
        call(names[0], integer=0xA4)
        call(names[1], integer=0xA4)
        call(names[4], 0x80B80001)
        call(names[3])
        call(names[4])
        call(names[5], 1)
        call(names[2], 0x80020010, integer=0xA4)
        call(names[6], 0x80B8000B)
        call(names[7], 0x80B80005, integer=0)
        call(names[8], 0x80B8000D, integer=0)
        asm.relative("48 c7 05", "param", "30 00 00 00")
        asm.relative("48 c7 05", "param_size", "88 00 00 00")
        call(names[8], 0x80B8000F, pointer="param")
        call(names[9])
        call(names[5], 0)
        call(names[2], integer=0xA4)
        call(names[1], 0x805A1001, integer=0xA4)
        call(names[0], integer=0xAC)
        call(names[10])
        call(names[11], 1)
        call(names[12], 0x80B8000D, integer=0)
        call(names[13])
        call(names[2], integer=0xAC)
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    imports = []
    for name in names:
        library = ("libSceSysmodule" if name.startswith("sceSysmodule") else
                   "libSceCommonDialog" if name.startswith("sceCommonDialog") else
                   "libSceMsgDialog" if name.startswith("sceMsgDialog") else
                   "libSceErrorDialog" if name.startswith("sceErrorDialog") else "libc")
        imports.append((name, library))
    return interfaces_fixture(asm.finish(), imports, data_size=512)


def time_fixture():
    asm = SyntheticCode()
    asm.labels.update(time_slot=4112, sleep_slot=4120, immediate_slot=4128)
    asm.emit("48 83 ec 08")
    asm.relative("ff 15", "time_slot")
    asm.emit("49 89 c4 bf b8 0b 00 00")  # 3000 microseconds
    asm.relative("ff 15", "sleep_slot")
    asm.emit("85 c0")
    asm.relative("0f 85", "fail")
    asm.relative("ff 15", "time_slot")
    asm.emit("4c 29 e0 48 3d b8 0b 00 00")
    asm.relative("0f 82", "fail")
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "immediate_slot")
    asm.emit("0f 0b")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "immediate_slot")
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [("sceKernelGetProcessTime", "libkernel"),
                                            ("sceKernelUsleep", "libkernel"), ("_Exit", "libc")])


def counter_fixture():
    names = ['sceKernelGetProcessTimeCounter', 'sceKernelGetProcessTimeCounterFrequency',
             'sceKernelReadTsc', 'sceKernelGetTscFrequency', 'sceKernelGetProcessTime', 'sceKernelUsleep', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.emit('48 83 ec 08')
    asm.relative('ff 15', names[1])
    asm.emit('48 3d 40 42 0f 00')  # Frequency > 1 MHz, full RAX comparison.
    asm.relative('0f 86', 'fail')
    asm.emit('49 89 c4')
    asm.relative('ff 15', names[3])
    asm.emit('4c 39 e0')
    asm.relative('0f 85', 'fail')
    asm.emit('0f ae e8 0f 31 0f ae e8 48 c1 e2 20 48 09 d0 49 89 c6')
    asm.relative('ff 15', names[2])
    asm.emit('4c 39 f0')  # Full 64-bit raw TSC must follow the actual RDTSC instruction.
    asm.relative('0f 82', 'fail')
    asm.emit('49 89 c6')
    asm.relative('ff 15', names[0])
    asm.emit('49 89 c5')
    asm.relative('ff 15', names[4])
    asm.emit('49 89 c7 bf b8 0b 00 00')
    asm.relative('ff 15', names[5])
    asm.emit('85 c0')
    asm.relative('0f 85', 'fail')
    asm.relative('ff 15', names[0])
    asm.emit('4c 39 e8')
    asm.relative('0f 86', 'fail')
    asm.emit('48 89 c3 4c 29 eb 4c 89 e0 31 d2 b9 e8 03 00 00 48 f7 f1 48 39 c3')
    asm.relative('0f 82', 'fail')  # Counter delta >= one millisecond of reported TSC ticks.
    asm.relative('ff 15', names[2])
    asm.emit('4c 39 f0')
    asm.relative('0f 86', 'fail')
    asm.relative('ff 15', names[4])
    asm.emit('4c 29 f8 48 3d b8 0b 00 00')
    asm.relative('0f 82', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', names[-1])
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', names[-1])
    return interfaces_fixture(asm.finish(), [(name, 'libc' if name == '_Exit' else 'libkernel') for name in names])


def screenshot_fixture():
    names = ["sceSysmoduleLoadModule", "sceSysmoduleUnloadModule", "sceSysmoduleIsLoaded",
             "sceScreenShotEnable", "sceScreenShotDisable", "sceScreenShotIsDisabled", "_Exit"]
    asm = SyntheticCode()
    for index, name in enumerate(names):
        asm.labels[name] = 4112 + index * 8
    asm.emit("48 83 ec 08")
    for name, argument, expected in ((names[0], 0x9C, 0), (names[2], 0x9C, 0),
                                     (names[4], None, 0), (names[5], None, 1),
                                     (names[3], None, 0), (names[5], None, 0),
                                     (names[1], 0x9C, 0), (names[2], 0x9C, 0x805A1001)):
        if argument is not None:
            asm.emit("bf " + struct.pack("<I", argument).hex(" "))
        asm.relative("ff 15", name)
        asm.emit("3d " + struct.pack("<I", expected).hex(" "))
        asm.relative("0f 85", "fail")
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [(name, "libSceSysmodule" if name.startswith("sceSysmodule") else
                                             "libc" if name == "_Exit" else "libSceScreenShot") for name in names])


def save_dialog_fixture(list_mode=False):
    names = ["sceSysmoduleLoadModule", "sceCommonDialogInitialize", "sceSaveDataDialogInitialize",
             "sceSaveDataDialogUpdateStatus", "sceSaveDataDialogGetResult", "sceSaveDataDialogClose",
             "sceSaveDataDialogOpen", "sceSaveDataDialogIsReadyToDisplay", "sceSaveDataDialogTerminate",
             "sceSysmoduleUnloadModule", "_Exit"]
    asm = SyntheticCode()
    for index, name in enumerate(names):
        asm.labels[name] = 4112 + index * 8
    asm.labels.update(save_param=4400, save_size=4448, save_mode=4452, save_operation=4456)

    def call(name, expected=0, integer=None, pointer=None):
        if integer is not None:
            asm.emit("bf " + struct.pack("<I", integer).hex(" "))
        if pointer:
            asm.relative("48 8d 3d", pointer)
        asm.relative("ff 15", name)
        asm.emit("3d " + struct.pack("<I", expected).hex(" "))
        asm.relative("0f 85", "fail")

    asm.emit("48 83 ec 08")
    call(names[0], integer=0xA0)
    call(names[1])
    call(names[7], 1)
    call(names[2])
    call(names[3], 1)
    call(names[4], 0x80B80005, integer=0)
    call(names[5], 0x80B8000B)
    call(names[6], 0x80B8000D, integer=0)
    if list_mode:
        asm.relative("48 c7 05", "save_param", "30 00 00 00")
        asm.relative("c7 05", "save_size", "98 00 00 00")
        asm.relative("c7 05", "save_mode", "01 00 00 00")
        asm.relative("c7 05", "save_operation", "01 00 00 00")
        call(names[6], pointer="save_param")
    call(names[8])
    call(names[3], 0)
    call(names[9], integer=0xA0)
    call(names[7], 0)
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [(name, "libSceSysmodule" if name.startswith("sceSysmodule") else
                                             "libSceCommonDialog" if name.startswith("sceCommonDialog") else
                                             "libc" if name == "_Exit" else "libSceSaveDataDialog") for name in names], data_size=512)


def storage_fixture():
    asm = SyntheticCode()
    names = ["sceUserServiceInitialize", "sceSaveDataInitialize3", "sceSaveDataTerminate", "_Exit"]
    for index, name in enumerate(names):
        asm.labels[name] = 4112 + index * 8
    asm.emit("48 83 ec 08")
    for name in names[:3]:
        asm.emit("31 ff")
        asm.relative("ff 15", name)
        asm.emit("85 c0")
        asm.relative("0f 85", "fail")
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [(names[0], "libSceUserService"), (names[1], "libSceSaveData"),
                                            (names[2], "libSceSaveData"), (names[3], "libc")])


def storage_mount_fixture(mode=1, expected=0x809f0008, blocks=0):
    names = ["sceUserServiceInitialize", "sceSaveDataInitialize3", "sceSaveDataMount2", "_Exit"]
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(request=4400, directory_pointer=4408, mount_mode=4424, result=4464, directory=4528)
    for index in range(8):
        asm.labels[f"result{index}"] = 4464 + index * 8
    asm.emit("48 83 ec 08 31 ff")
    for name in names[:2]:
        asm.relative("ff 15", name)
        asm.emit("85 c0")
        asm.relative("0f 85", "fail")
    asm.relative("c7 05", "request", "00 10 00 00")
    asm.relative("48 8d 05", "directory")
    asm.relative("48 89 05", "directory_pointer")
    asm.relative("c7 05", "mount_mode", struct.pack("<I", mode).hex(" "))
    asm.labels['mount_blocks'] = 4416
    asm.relative('48 c7 05', 'mount_blocks', struct.pack('<I', blocks).hex(' '))
    # Authored directory token; fixture memory is zero-filled after this value.
    asm.emit("48 b8 " + b"Fixture\0".hex(" "))
    asm.relative("48 89 05", "directory")
    asm.emit("48 b8 " + (b"\xa5" * 8).hex(" "))
    for index in range(8):
        asm.relative("48 89 05", f"result{index}")
    asm.relative("48 8d 3d", "request")
    asm.relative("48 8d 35", "result")
    asm.relative("ff 15", names[2])
    asm.emit('3d ' + struct.pack('<I', expected).hex(' '))
    asm.relative("0f 85", "fail")
    asm.emit("48 b8 " + (b"\xa5" * 8).hex(" "))
    for index in range(8):
        asm.relative("48 39 05", f"result{index}")
        asm.relative("0f 85", "fail")
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", names[3])
    asm.emit("0f 0b")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", names[3])
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [(names[0], "libSceUserService"), (names[1], "libSceSaveData"),
                                            (names[2], "libSceSaveData"), (names[3], "libc")], data_size=512)


def save_io_fixture(*, reopen=False, exhaust=False):
    names = ['sceUserServiceInitialize', 'sceSaveDataInitialize3', 'sceSaveDataDirNameSearch', 'sceSaveDataMount2',
             'sceSaveDataSetParam', 'sceSaveDataSaveIcon', 'sceKernelOpen', 'sceKernelWrite', 'sceKernelRead',
             'sceKernelClose', 'sceKernelFstat', 'sceKernelLseek', 'sceSaveDataUmount', 'sceSaveDataTerminate', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(directory=4352, mount_request=4400, mount_directory=4408, mount_blocks=4416, mount_mode=4424,
                      mounted=4480, mount_status=4508, parameter=4560, parameter_user=5840, icon=6128, icon_data=6136,
                      icon_size=6144, condition=6208, condition_pattern=6224, condition_order=6236, search=6272,
                      search_names=6280, search_capacity=6288, search_set=6292, names=6400, png=6464,
                      payload=6480, readback=6528, read_tail=6544, stat=6592, stat_size=6664, stat_tail=6712, path=6800)

    def word(label, value):
        asm.relative('c7 05', label, struct.pack('<I', value & 0xffffffff).hex(' '))

    def pointer(label, target):
        asm.relative('48 8d 05', target)
        asm.relative('48 89 05', label)

    def qword(label, value):
        asm.emit('48 b8 ' + struct.pack('<Q', value).hex(' '))
        asm.relative('48 89 05', label)

    def call(name, expected=0, wide=False):
        asm.relative('ff 15', name)
        if expected is not None:
            asm.emit(('48 3d ' if wide else '3d ') + struct.pack('<I', expected & 0xffffffff).hex(' '))
            asm.relative('0f 85', 'fail')

    def equal(label, value):
        asm.relative('81 3d', label, struct.pack('<I', value).hex(' '))
        asm.relative('0f 85', 'fail')

    asm.emit('48 83 ec 08 31 ff')
    call('sceUserServiceInitialize')
    asm.emit('31 ff'); call('sceSaveDataInitialize3')
    qword('directory', int.from_bytes(b'Fixture\0', 'little'))
    word('condition', 0x1000); pointer('condition_pattern', 'directory'); word('condition_order', 1)
    pointer('search_names', 'names'); word('search_capacity', 2); word('search_set', 73)
    asm.relative('48 8d 3d', 'condition'); asm.relative('48 8d 35', 'search'); call('sceSaveDataDirNameSearch')
    equal('search', 1 if reopen else 0); equal('search_set', 1 if reopen else 0)
    word('mount_request', 0x1000); pointer('mount_directory', 'directory'); qword('mount_blocks', 96); word('mount_mode', 1 if reopen else 6)
    asm.relative('48 8d 3d', 'mount_request'); asm.relative('48 8d 35', 'mounted'); call('sceSaveDataMount2')
    equal('mount_status', 0 if reopen else 1)
    equal('mounted', int.from_bytes(b'/sav', 'little'))
    qword('parameter', int.from_bytes(b'Synthet\0', 'little')); word('parameter_user', 73)
    asm.relative('48 8d 3d', 'mounted'); asm.emit('31 f6'); asm.relative('48 8d 15', 'parameter'); asm.emit('b9 30 05 00 00')
    call('sceSaveDataSetParam', 0x809f000d if reopen else 0)
    qword('png', int.from_bytes(b'\x89PNG\r\n\x1a\n', 'little')); pointer('icon', 'png'); qword('icon_data', 8); qword('icon_size', 8)
    asm.relative('48 8d 3d', 'mounted'); asm.relative('48 8d 35', 'icon'); call('sceSaveDataSaveIcon', 0x809f000d if reopen else 0)
    path_bytes = b'/savedata0/data.bin\0'
    for index in range(0, len(path_bytes), 8):
        label = f'path_{index}'; asm.labels[label] = 6800 + index
        qword(label, int.from_bytes(path_bytes[index:index + 8].ljust(8, b'\0'), 'little'))
    asm.relative('48 8d 3d', 'path'); asm.emit('be ' + struct.pack('<I', 0 if reopen else 0x602).hex(' ') + ' ba b6 01 00 00')
    call('sceKernelOpen', None); asm.emit('85 c0'); asm.relative('0f 8e', 'fail'); asm.emit('41 89 c4')
    qword('payload', 0x8877665544332211); asm.labels['payload_tail'] = 6488; qword('payload_tail', 0xff00ee11dd22cc33)
    asm.emit('44 89 e7'); asm.relative('48 8d 35', 'payload'); asm.emit('ba 10 00 00 00')
    call('sceKernelWrite', 0x80020009 if reopen else 16, wide=True)
    word('stat_tail', 73); asm.emit('44 89 e7'); asm.relative('48 8d 35', 'stat'); call('sceKernelFstat')
    equal('stat_size', 16); equal('stat_tail', 73)
    asm.emit('44 89 e7 31 f6 31 d2'); call('sceKernelLseek', 0, wide=True)
    word('read_tail', 91); asm.emit('44 89 e7'); asm.relative('48 8d 35', 'readback'); asm.emit('ba 20 00 00 00'); call('sceKernelRead', 16, wide=True)
    for index, expected in ((0, 0x44332211), (4, 0x88776655), (8, 0xdd22cc33), (12, 0xff00ee11)):
        label = f'read_check_{index}'; asm.labels[label] = 6528 + index; equal(label, expected)
    equal('read_tail', 91)
    asm.relative('48 8d 3d', 'mounted'); call('sceSaveDataUmount', 0x809f0003)
    asm.emit('44 89 e7'); call('sceKernelClose')
    asm.emit('44 89 e7'); asm.relative('48 8d 35', 'readback'); asm.emit('ba 01 00 00 00'); call('sceKernelRead', 0x80020009, wide=True)
    if exhaust:
        asm.emit('41 bd 03 00 00 00')
        asm.label('open_all')
        asm.relative('48 8d 3d', 'path'); asm.emit('31 f6 31 d2'); call('sceKernelOpen', None)
        asm.emit('44 39 e8'); asm.relative('0f 85', 'fail')
        asm.emit('41 ff c5 41 81 fd 00 04 00 00'); asm.relative('0f 85', 'open_all')
        asm.relative('48 8d 3d', 'path'); asm.emit('be 02 06 00 00 ba b6 01 00 00'); call('sceKernelOpen', 0x80020018)
        asm.emit('bf 03 00 00 00'); asm.relative('48 8d 35', 'stat'); call('sceKernelFstat'); equal('stat_size', 16)
        asm.emit('41 bd 03 00 00 00')
        asm.label('close_all')
        asm.emit('44 89 ef'); call('sceKernelClose')
        asm.emit('41 ff c5 41 81 fd 00 04 00 00'); asm.relative('0f 85', 'close_all')
    asm.relative('48 8d 3d', 'mounted'); call('sceSaveDataUmount')
    call('sceSaveDataTerminate')
    asm.emit('bf 07 00 00 00'); asm.relative('ff 15', '_Exit'); asm.emit('0f 0b')
    asm.label('fail'); asm.emit('bf 63 00 00 00'); asm.relative('ff 15', '_Exit'); asm.emit('0f 0b')
    imports = [(name, 'libc' if name == '_Exit' else 'libSceUserService' if name.startswith('sceUser') else
                'libkernel' if name.startswith('sceKernel') else 'libSceSaveData') for name in names]
    return interfaces_fixture(asm.finish(), imports, data_size=4096)


def np_request_fixture(*, library='libSceNpManager', module='libSceNpManager'):
    names = ['sceNpCreateRequest', 'sceNpDeleteRequest', 'sceNpGetAccountLanguageA', 'sceNpGetParentalControlInfoA',
             'sceNpCheckNpAvailabilityA', 'sceNpGetNpReachabilityState', 'sceNpGetAccountCountryA', 'sceNpGetAccountIdA',
             'sceUserServiceInitialize', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(request=4400, request_tail=4404, language=4416, language2=4424, language_tail=4432,
                      age=4440, info=4448, reachability=4456, country=4464, account=4472)
    asm.emit('48 83 ec 08 31 ff')

    def call(index, expected=0):
        asm.relative('ff 15', names[index])
        asm.emit('3d ' + struct.pack('<I', expected & 0xffffffff).hex(' '))
        asm.relative('0f 85', 'fail')

    def create():
        asm.relative('ff 15', names[0])
        asm.emit('85 c0')
        asm.relative('0f 8e', 'fail')
        asm.relative('89 05', 'request')

    def request():
        asm.relative('8b 3d', 'request')

    def check64(label):
        asm.emit('48 b8 73 73 73 73 73 73 73 73')
        asm.relative('48 39 05', label)
        asm.relative('0f 85', 'fail')

    call(8)
    asm.relative('c7 05', 'request_tail', '73 73 73 73')
    for label in ('language', 'language2', 'language_tail', 'age', 'info', 'reachability', 'country', 'account'):
        asm.emit('48 b8 73 73 73 73 73 73 73 73')
        asm.relative('48 89 05', label)
    create()
    for expected in (0x80550006, 0x80550003):
        request()
        asm.emit('be 00 10 00 00')
        call(4, expected)
    request()
    call(1)
    request()
    call(1, 0x80550014)
    create()
    request()
    asm.emit('be 00 10 00 00 31 d2')
    call(2, 0x80550003)
    request()
    asm.emit('be 00 10 00 00')
    asm.relative('48 8d 15', 'language')
    call(2, 0x80550006)
    for label in ('language', 'language2', 'language_tail'):
        check64(label)
    request()
    call(1)
    create()
    request()
    asm.emit('be 00 10 00 00')
    asm.relative('48 8d 15', 'age')
    asm.relative('48 8d 0d', 'info')
    call(3, 0x80550006)
    check64('age')
    check64('info')
    request()
    call(1)
    asm.emit('bf 00 10 00 00')
    asm.relative('48 8d 35', 'reachability')
    call(5)
    asm.emit('48 b8 00 00 00 00 73 73 73 73')
    asm.relative('48 39 05', 'reachability')
    asm.relative('0f 85', 'fail')
    for index, label in ((6, 'country'), (7, 'account')):
        asm.emit('bf 00 10 00 00')
        asm.relative('48 8d 35', label)
        call(index, 0x80550006)
        check64(label)
    asm.relative('81 3d', 'request_tail', '73 73 73 73')
    asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', '_Exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', '_Exit')
    return interfaces_fixture(asm.finish(), [(name, 'libSceUserService' if index == 8 else 'libc' if index == 9 else library, 2,
                                             'libSceUserService' if index == 8 else 'libc' if index == 9 else module) for index, name in enumerate(names)])


def optional_modules_fixture():
    names = ['sceSysmoduleLoadModule', 'sceSysmoduleIsLoaded', 'sceSysmoduleUnloadModule', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.emit('48 83 ec 08')
    for module_id in (7, 0x81, 0x85, 0x8d, 0xb7, 0xe7, 0xb2, 0x1e, 0x21):
        for operation in (names[1], names[0], names[0], names[1], names[2], names[1]):
            asm.emit('bf ' + struct.pack('<I', module_id).hex(' '))
            asm.relative('ff 15', operation)
            asm.emit('3d 01 10 5a 80')
            asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', names[-1])
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', names[-1])
    return interfaces_fixture(asm.finish(), [(name, 'libc' if name == '_Exit' else 'libSceSysmodule') for name in names])


def net_state_fixture(*, library='libSceNetCtl', module='libSceNetCtl', mode='normal'):
    asm = SyntheticCode()
    asm.labels.update(state=4112, exit=4120, value=4400, tail=4404)
    asm.emit('48 83 ec 08')
    if mode == 'normal':
        asm.relative('c7 05', 'value', '49 00 00 00')
        asm.relative('c7 05', 'tail', '49 00 00 00')
        asm.relative('48 8d 3d', 'value')
    elif mode == 'readonly':
        asm.relative('48 8d 3d', 'readonly')
    else:
        asm.emit('bf ' + struct.pack('<I', 0 if mode == 'null' else 1).hex(' '))
    asm.relative('ff 15', 'state')
    asm.emit('3d ' + struct.pack('<I', 0 if mode == 'normal' else 0x80412107).hex(' '))
    asm.relative('0f 85', 'fail')
    if mode == 'normal':
        asm.relative('83 3d', 'value', '00')
        asm.relative('0f 85', 'fail')
        asm.relative('83 3d', 'tail', '49')
        asm.relative('0f 85', 'fail')
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', 'exit')
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', 'exit')
    asm.label('readonly')
    asm.code.extend(b'\x49' * 8)
    return interfaces_fixture(asm.finish(), [('sceNetCtlGetState', library, 2, module), ('_Exit', 'libc')], data_size=512)


def np_extended_fixture():
    names = ['sceUserServiceInitialize', 'sceNpSetContentRestriction', 'sceNpSetNpTitleId',
             'sceNpCreateAsyncRequest', 'sceNpCheckNpAvailabilityA', 'sceNpPollAsync',
             'sceNpAbortRequest', 'sceNpDeleteRequest', '_Exit']
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(rating=4400, country_pointer=4416, country=4432, title=4448, secret=4480,
                      asynchronous=4624, result=4660, tail=4664)
    asm.emit('48 83 ec 08')

    def call(name, expected=0):
        asm.relative('ff 15', name)
        asm.emit('3d ' + struct.pack('<I', expected).hex(' '))
        asm.relative('0f 85', 'fail')

    def pointer(register, label):
        asm.relative({'rdi': '48 8d 3d', 'rsi': '48 8d 35'}[register], label)

    def request():
        pointer('rdi', 'asynchronous')
        asm.relative('ff 15', names[3])
        asm.emit('85 c0')
        asm.relative('0f 8e', 'fail')
        asm.emit('41 89 c4')

    def poll(expected=0):
        asm.emit('44 89 e7')
        pointer('rsi', 'result')
        call(names[5], expected)

    def output(value):
        asm.relative('81 3d', 'result', struct.pack('<I', value).hex(' '))
        asm.relative('0f 85', 'fail')
        asm.relative('83 3d', 'tail', '49')
        asm.relative('0f 85', 'fail')

    asm.emit('31 ff')
    call(names[0])
    asm.emit('31 ff')
    call(names[1], 0x80550003)
    asm.relative('48 8d 05', 'country')
    asm.relative('48 89 05', 'country_pointer')
    pointer('rdi', 'rating')
    call(names[1])
    asm.relative('c6 05', 'rating', '17')
    pointer('rdi', 'rating')
    call(names[1], 0x80550011)
    pointer('rdi', 'title'); pointer('rsi', 'secret')
    call(names[2])
    request(); poll(0x80550015); output(73)
    asm.emit('44 89 e7 be 00 10 00 00')
    call(names[4])
    poll(); output(0x80550006)
    asm.emit('44 89 e7'); call(names[6])
    poll(); output(0x80550006)
    asm.emit('44 89 e7'); call(names[7])
    poll(0x80550014); output(0x80550006)
    request()
    asm.emit('44 89 e7'); call(names[6])
    poll(); output(0x80550012)
    asm.emit('44 89 e7 be 00 10 00 00'); call(names[4], 0x80550012)
    asm.emit('44 89 e7'); call(names[7])
    asm.emit('bf 07 00 00 00')
    asm.relative('ff 15', names[-1])
    asm.label('fail')
    asm.emit('bf 63 00 00 00')
    asm.relative('ff 15', names[-1])
    code = asm.finish()
    imports = [(name, 'libc' if name == '_Exit' else 'libSceUserService' if index == 0 else 'libSceNpManager')
               for index, name in enumerate(names)]
    image = interfaces_fixture(code, imports, data_size=2048)
    data_start = 96 + 32 + len(code) + 32 - 4096
    struct.pack_into('<Qb3xiQ', image, data_start + 4400, 24, 12, 1, 0)
    image[data_start + 4432:data_start + 4440] = b'JP\0\0\x11\0\0\0'
    image[data_start + 4448:data_start + 4464] = b'FIXTURE_0001\0\0\0\0'
    image[data_start + 4480:data_start + 4608] = b'\x5a' * 128
    struct.pack_into('<QQi4x', image, data_start + 4624, 24, 63, 700)
    struct.pack_into('<ii', image, data_start + 4660, 73, 73)
    return image


def np_fixture(library="libSceNpManager"):
    names = ["sceUserServiceInitialize", "sceNpRegisterStateCallbackA", "sceNpCheckCallback",
             "sceNpUnregisterStateCallbackA", "sceNpGetState", "_Exit"]
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(count=4400, user=4404, state=4408, argument=4416, queried=4424)
    asm.emit("48 83 ec 08 31 ff")

    def call(name, expected=0):
        asm.relative("ff 15", name)
        asm.emit("3d " + struct.pack("<I", expected).hex(" "))
        asm.relative("0f 85", "fail")

    call(names[0])
    asm.relative("48 8d 3d", "callback")
    asm.emit("48 be 21 43 65 87 09 ba dc fe")
    asm.relative("ff 15", names[1])
    asm.emit("85 c0")
    asm.relative("0f 8e", "fail")
    asm.emit("41 89 c4")
    call(names[2])
    call(names[2])
    for label, value in (("count", 0), ("user", 0), ("state", 0)):
        asm.relative("81 3d", label, struct.pack("<I", value).hex(" "))
        asm.relative("0f 85", "fail")
    asm.emit("31 c0")
    asm.relative("48 39 05", "argument")
    asm.relative("0f 85", "fail")
    asm.emit("bf 00 10 00 00")
    asm.relative("48 8d 35", "queried")
    call(names[4])
    asm.relative("83 3d", "queried", "01")
    asm.relative("0f 85", "fail")
    asm.emit("44 89 e7")
    call(names[3])
    asm.emit("44 89 e7")
    call(names[3], 0x80550009)
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    asm.label("callback")
    asm.relative("ff 05", "count")
    asm.relative("89 3d", "user")
    asm.relative("89 35", "state")
    asm.relative("48 89 15", "argument")
    asm.emit("c3")
    return interfaces_fixture(asm.finish(), [(name, "libc" if name == "_Exit" else
                                            "libSceUserService" if name.startswith("sceUser") else library)
                                           for name in names], data_size=512)


def pad_fixture(library="libScePad"):
    names = ["sceUserServiceInitialize", "scePadInit", "scePadOpen", "scePadRead", "scePadReadState",
             "scePadGetControllerInformation", "scePadClose", "_Exit"]
    asm = SyntheticCode()
    asm.labels.update({name: 4112 + index * 8 for index, name in enumerate(names)})
    asm.labels.update(data=4400, orientation_w=4424, tail=4520, information=4544)
    asm.emit("48 83 ec 08 31 ff")

    def call(name, expected=0):
        asm.relative("ff 15", name)
        asm.emit("3d " + struct.pack("<I", expected).hex(" "))
        asm.relative("0f 85", "fail")

    call(names[0])
    call(names[1])
    asm.emit("bf 00 10 00 00 31 f6 31 d2 31 c9")
    asm.relative("ff 15", names[2])
    asm.emit("85 c0")
    asm.relative("0f 8e", "fail")
    asm.emit("41 89 c4")
    asm.relative("c7 05", "tail", "78 56 34 12")
    for name, expected in ((names[3], 1), (names[4], 0)):
        asm.emit("44 89 e7 ba 01 00 00 00")
        asm.relative("48 8d 35", "data")
        call(name, expected)
        asm.relative("81 3d", "orientation_w", "00 00 80 3f")
        asm.relative("0f 85", "fail")
        asm.relative("81 3d", "tail", "78 56 34 12")
        asm.relative("0f 85", "fail")
    asm.emit("44 89 e7")
    asm.relative("48 8d 35", "information")
    call(names[5])
    asm.emit("44 89 e7")
    call(names[6])
    asm.emit("44 89 e7")
    call(names[6], 0x80920003)
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [(name, "libc" if name == "_Exit" else
                                            "libSceUserService" if name.startswith("sceUser") else library)
                                           for name in names], data_size=512)


def user_fixture():
    names = ["sceUserServiceInitialize", "sceUserServiceGetInitialUser", "sceUserServiceGetLoginUserIdList",
             "sceUserServiceGetEvent", "sceUserServiceGetUserName", "sceUserServiceTerminate", "_Exit"]
    asm = SyntheticCode()
    for index, name in enumerate(names):
        asm.labels[name] = 4112 + index * 8
    asm.labels.update(users=4096, user_event=4400, event_user=4404, user_name=4416, name_end=4424, initial=4480)

    def call(name, expected=0, integer=None, pointer=None):
        if integer is not None:
            asm.emit("bf " + struct.pack("<I", integer).hex(" "))
        if pointer:
            asm.relative("48 8d 3d", pointer)
        asm.relative("ff 15", name)
        asm.emit("3d " + struct.pack("<I", expected).hex(" "))
        asm.relative("0f 85", "fail")

    def compare(label, value):
        asm.relative("81 3d", label, struct.pack("<I", value).hex(" "))
        asm.relative("0f 85", "fail")

    asm.emit("48 83 ec 08")
    call(names[0], integer=0)
    call(names[0], 0x80960003, integer=0)
    call(names[1], pointer="initial")
    compare("initial", 0x1000)
    call(names[2], pointer="users")
    compare("users", 0x1000)
    for index in range(1, 4):
        label = f"unused_user{index}"
        asm.labels[label] = 4096 + index * 4
        compare(label, 0xFFFFFFFF)
    call(names[3], pointer="user_event")
    compare("user_event", 0)
    compare("event_user", 0x1000)
    call(names[3], 0x80960007, pointer="user_event")
    asm.relative("48 8d 35", "user_name")
    asm.emit("ba 20 00 00 00")
    call(names[4], integer=0x1000)
    asm.emit("48 b8 50 6c 61 79 65 72 20 31")
    asm.relative("48 39 05", "user_name")
    asm.relative("0f 85", "fail")
    asm.relative("80 3d", "name_end", "00")
    asm.relative("0f 85", "fail")
    call(names[5])
    call(names[1], 0x80960002, pointer="initial")
    asm.emit("bf 07 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    asm.label("fail")
    asm.emit("bf 63 00 00 00")
    asm.relative("ff 15", "_Exit")
    asm.emit("0f 0b")
    return interfaces_fixture(asm.finish(), [(name, "libc" if name == "_Exit" else "libSceUserService")
                                            for name in names], data_size=512)


class FormatTests(unittest.TestCase):
    def test_prx_preparation_preserves_zero_init_and_exports(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'fixture.prx'
            source.write_bytes(synthetic_prx_elf())
            packed, metadata = module_preparer().prepare(source, {}, module=True)
            self.assertEqual(packed[:8], b'KTMODULE')
            self.assertEqual(struct.unpack_from('<4Q', packed, 96), (0, 16, 3, 1))
            self.assertEqual(metadata['exports'][0]['library'], 'libFixture')

    def test_prx_preparation_rejects_tls_arrays_missing_init_and_duplicate_exports(self):
        for option, message in (('tls', 'TLS'), ('arrays', 'arrays'), ('duplicate', 'Duplicate'), ('init', 'DT_INIT')):
            with self.subTest(option=option), tempfile.TemporaryDirectory() as directory:
                source = Path(directory) / 'fixture.prx'
                source.write_bytes(synthetic_prx_elf(**{option: option != 'init'}))
                with self.assertRaisesRegex(ValueError, message):
                    module_preparer().prepare(source, {}, module=True)

    def test_prx_preparation_omits_only_unreferenced_untyped_weak_declarations(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'fixture.prx'
            raw = synthetic_prx_elf(untyped_binding=2)
            source.write_bytes(raw)
            packed, metadata = module_preparer().prepare(source, {}, module=True)
            self.assertEqual(struct.unpack_from('<Q', packed, 40)[0], 0)
            self.assertEqual(metadata['imports'], [])
            omitted = metadata['omitted_unreferenced_weak_imports']
            self.assertEqual([(s['name'], s['type'], s['binding']) for s in omitted], [('OptionalFixtureSymbol', 0, 2)])
            self.assertEqual(source.read_bytes(), raw)

    def test_prx_preparation_rejects_referenced_untyped_weak_and_strong_symbols(self):
        for binding, referenced in ((2, True), (1, False), (1, True)):
            with self.subTest(binding=binding, referenced=referenced), tempfile.TemporaryDirectory() as directory:
                source = Path(directory) / 'fixture.prx'
                source.write_bytes(synthetic_prx_elf(untyped_binding=binding, referenced=referenced))
                with self.assertRaisesRegex(ValueError, 'Unsupported import symbol kind'):
                    module_preparer().prepare(source, {}, module=True)
    def test_known_nids(self):
        self.assertEqual(nid("_init_env"), "bzQExy189ZI")
        self.assertEqual(nid("atexit"), "8G2LB+A3rzg")

    def test_identifier_encoding(self):
        self.assertEqual([encode_id(v) for v in (0, 28, 63, 64, 4095, 4096)],
                         ["A", "c", "-", "BA", "--", "BAA"])

    def test_valid_synthetic_elf(self):
        elf = Elf(synthetic_elf())
        self.assertEqual(elf.image_size, 4096)
        self.assertEqual(elf.inventory()["imports"], [])

    def test_truncated_elf_rejected(self):
        with self.assertRaises(ValueError):
            Elf(synthetic_elf()[:20])

    def test_wrong_architecture_rejected(self):
        raw = synthetic_elf()
        struct.pack_into("<H", raw, 18, 183)
        with self.assertRaisesRegex(ValueError, "x86-64"):
            Elf(raw)

    def test_file_extent_rejected(self):
        raw = synthetic_elf()
        struct.pack_into("<Q", raw, 64 + 8, 0x10000)
        with self.assertRaisesRegex(ValueError, "bounds"):
            Elf(raw)

    def test_entry_in_zero_fill_rejected(self):
        raw = synthetic_elf()
        struct.pack_into("<Q", raw, 24, 128)
        with self.assertRaisesRegex(ValueError, "Entry"):
            Elf(raw)

    def test_unterminated_dynamic_table_rejected(self):
        raw = synthetic_elf()
        struct.pack_into("<qQ", raw, 0x180 + 64, 0x6100003B, 24)
        with self.assertRaisesRegex(ValueError, "Unterminated"):
            Elf(raw)

    def test_bad_symbol_string_rejected(self):
        raw = synthetic_elf()
        struct.pack_into("<I", raw, 0x208, 20)
        with self.assertRaisesRegex(ValueError, "String offset"):
            Elf(raw)


@unittest.skipUnless(os.name == "nt" and (ROOT / "out/native/KetsuiNativeProbe.exe").exists(),
                     "Build the native Windows probe first")
class RuntimeTests(unittest.TestCase):
    def test_media_foundation_decodes_authored_h264_aac_pixels_pcm_pts_and_rewind_readonly(self):
        fixture = ROOT / 'tests/fixtures/media/red-aac-v1.mp4'
        self.assertEqual(hashlib.sha256(fixture.read_bytes()).hexdigest(),
                         'a9bd25cf8702fc6b02dfbe57458c511915e69520f2265c4842996a9638424e66')
        with tempfile.TemporaryDirectory(prefix='kt-media-') as directory:
            app, patch, _ = content_tree(directory)
            shutil.copyfile(fixture, app / 'fixture.mp4')
            (app / 'not-media.mp4').write_bytes(b'authored malformed media\0' * 32)
            before = {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                      for root in (app, patch) for path in root.rglob('*') if path.is_file()}
            result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--media-self-test',
                                     str(app), str(patch)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            cases = [event for event in events if event['event'] == 'media_self_test_case']
            self.assertEqual(len(cases), 10)
            self.assertTrue(all(event['passed'] for event in cases))
            playback_cases = [event for event in events if event['event'] == 'media_playback_self_test_case']
            self.assertEqual(len(playback_cases), 15)
            self.assertTrue(all(event['passed'] for event in playback_cases))
            self.assertEqual(before, {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                for root in (app, patch) for path in root.rglob('*') if path.is_file()})
            self.assertTrue(any(event['event'] == 'native_media_shutdown' and event['hresult'] == 0 for event in events))

    def test_puts_sysv_nonnegative_result_empty_line_and_separate_console_sink(self):
        result = self.invoke(puts_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        self.assertEqual(result.stderr, '[guest stdout] authored console line\n[guest stdout] \n')
        self.assertNotIn('authored console line', result.stdout)
        for line in result.stdout.splitlines():
            json.loads(line)

    def test_puts_invalid_pointer_is_controlled_and_namespace_is_exact(self):
        result = self.invoke(puts_fixture(invalid=True), '--enter')
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertIn('puts_invalid_string', result.stdout)
        for library, module in (('libc', 'libkernel'), ('libkernel', 'libc')):
            result = self.invoke(puts_fixture(library=library, module=module), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertIn('unimplemented_import', result.stdout)

    def test_avplayer_mf_lifecycle_sysv_allocators_reentrancy_idle_and_stale_handles(self):
        result = self.invoke(avplayer_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
        startup = [e for e in events if e['event'] == 'native_media_startup']
        self.assertEqual(len(startup), 1)
        self.assertEqual(startup[0]['hresult'], 0)
        self.assertEqual(len([e for e in events if e['event'] == 'native_media_shutdown' and e['hresult'] == 0]), 1)
        self.assertEqual(len([e for e in events if e['event'] == 'native_avplayer_init' and e['source_count'] == 0]), 2)
        self.assertEqual(len([e for e in events if e['event'] == 'native_avplayer_close']), 2)

    def test_avplayer_invalid_allocator_and_allocation_rollback_do_not_leak_or_publish(self):
        for allocation in ('null', 'unaligned', 'bad_callback'):
            with self.subTest(allocation=allocation):
                result = self.invoke(avplayer_fixture(allocation=allocation), '--enter')
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
                self.assertNotIn('native_avplayer_init', result.stdout)
                self.assertIn('native_media_shutdown', result.stdout)

    def test_avplayer_requires_exact_module_and_library(self):
        for library, module in (('libSceAvPlayer', 'libc'), ('libc', 'libSceAvPlayer')):
            result = self.invoke(avplayer_fixture(library=library, module=module), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertIn('sceAvPlayerInit', result.stdout)

    def test_avplayer_start_without_source_fails_instead_of_reporting_fake_playback(self):
        result = self.invoke(avplayer_fixture(source=True), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        self.assertNotIn('native_avplayer_playback', result.stdout)

    def test_avplayer_sysv_real_source_metadata_callback_reentry_enable_and_rollback(self):
        with tempfile.TemporaryDirectory(prefix='kt-avsource-') as directory:
            app, patch, _ = content_tree(directory)
            shutil.copyfile(ROOT / 'tests/fixtures/media/red-aac-v1.mp4', app / 'fixture.mp4')
            (app / 'not-media.mp4').write_bytes(b'authored invalid MP4' * 32)
            before = {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                      for root in (app, patch) for path in root.rglob('*') if path.is_file()}
            for source in ('metadata', 'missing', 'malformed'):
                with self.subTest(source=source):
                    result = self.invoke(avplayer_fixture(source=source), '--enter', app_directory=app, patch_directory=patch)
                    self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
                    self.assertEqual('native_avplayer_source_ready' in result.stdout, source == 'metadata')
                    self.assertIn('native_media_shutdown', result.stdout)
            self.assertEqual(before, {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                for root in (app, patch) for path in root.rglob('*') if path.is_file()})

    def test_avplayer_sysv_real_frames_pause_resume_eof_controller_and_allocator_lifetime(self):
        with tempfile.TemporaryDirectory(prefix='kt-avframes-') as directory:
            app, patch, _ = content_tree(directory)
            shutil.copyfile(ROOT / 'tests/fixtures/media/red-aac-v1.mp4', app / 'fixture.mp4')
            before = hashlib.sha256((app / 'fixture.mp4').read_bytes()).hexdigest()
            for fail_audio_once in (False, True):
                with self.subTest(fail_audio_once=fail_audio_once):
                    result = self.invoke(avplayer_frame_fixture(fail_audio_once=fail_audio_once), '--enter', app_directory=app, patch_directory=patch)
                    self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
                    events = [json.loads(line) for line in result.stdout.splitlines()]
                    end = [event for event in events if event['event'] == 'native_avplayer_natural_end']
                    self.assertEqual(len(end), 1)
                    self.assertEqual((end[0]['video_frames'], end[0]['audio_packets']), (2, 37))
                    self.assertTrue(any(event['event'] == 'native_media_shutdown' and event['hresult'] == 0 for event in events))
                    self.assertEqual('native_avplayer_frame_allocation_failure' in result.stdout, fail_audio_once)
            self.assertEqual(before, hashlib.sha256((app / 'fixture.mp4').read_bytes()).hexdigest())

    def test_sfo_serialization_round_trip_types_validation_and_bounds(self):
        result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--sfo-self-test'],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        cases = [event for line in result.stdout.splitlines() if (event := json.loads(line))['event'] == 'sfo_self_test_case']
        self.assertEqual(len(cases), 7)
        self.assertTrue(all(event['passed'] for event in cases))

    def test_manual_process_sample_resumes_synthetic_target_and_rejects_other_programs(self):
        executable = str(ROOT / 'out/native/KetsuiNativeProbe.exe')
        for argument in ('0', '', 'not-a-pid', '4294967296', str(os.getpid())):
            with self.subTest(argument=argument):
                rejected = subprocess.run([executable, '--sample-process', argument], capture_output=True, text=True, timeout=5)
                self.assertEqual(rejected.returncode, 2, rejected.stdout + rejected.stderr)
        asm = SyntheticCode()
        asm.labels.update(sleep_slot=4112, exit_slot=4120)
        asm.emit('48 83 ec 08 bf c0 c6 2d 00')  # Sleep three seconds in the synthetic guest.
        asm.relative('ff 15', 'sleep_slot')
        asm.emit('bf 07 00 00 00')
        asm.relative('ff 15', 'exit_slot')
        asm.emit('0f 0b')
        data = interfaces_fixture(asm.finish(), [('sceKernelUsleep', 'libkernel'), ('_Exit', 'libc')])
        with tempfile.TemporaryDirectory(prefix='kt-sample-') as directory:
            path = Path(directory) / 'synthetic.ktprobe'
            path.write_bytes(data)
            target = subprocess.Popen([executable, str(path), '--enter'], stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, text=True, cwd=ROOT)
            try:
                # Startup may report editable key mappings before enter_game.
                for _ in range(512):
                    line = target.stdout.readline()
                    if not line:
                        self.fail('Synthetic sampling target exited before entering its guest')
                    if json.loads(line)['event'] == 'enter_game':
                        break
                else:
                    self.fail('Synthetic target did not enter its guest')
                sampled = subprocess.run([executable, '--sample-process', str(target.pid)], capture_output=True,
                                         text=True, timeout=5)
                self.assertEqual(sampled.returncode, 0, sampled.stdout + sampled.stderr)
                self.assertIn('reason=manual_sample', sampled.stderr)
                self.assertIn('failed=0', sampled.stderr)
                remaining, errors = target.communicate(timeout=6)
                self.assertEqual(target.returncode, 7, remaining + errors)
            finally:
                if target.poll() is None:
                    target.kill()
                    target.communicate(timeout=5)

    def invoke(self, data, mode="--validate", *, user_directory=None, modules=None, app_directory=None, patch_directory=None, addons_directory=None, mouse_button=None, texture_validation=None, attachment_validation=None, diagnostic_tap=None, diagnostic_circle_hold=None, gpu_runtime=None, input_config=None, read_patches=None, timeout=10):
        with tempfile.TemporaryDirectory(prefix="kt-probe-") as directory:
            path = Path(directory) / "synthetic.ktprobe"
            path.write_bytes(data)
            command = [str(ROOT / "out/native/KetsuiNativeProbe.exe"), str(path), mode]
            if input_config is not None:
                command += ['--input-config', str(input_config)]
            if read_patches is not None:
                command += ['--read-patches', str(read_patches)]
            if diagnostic_tap is not None:
                command += ['--diagnostic-tap-ms', str(diagnostic_tap)]
            if diagnostic_circle_hold is not None:
                command += ['--diagnostic-circle-hold-ms', str(diagnostic_circle_hold)]
            if gpu_runtime is not None:
                command += ['--gpu-runtime', str(gpu_runtime)]
            if mouse_button is not None:
                command += ['--mouse-left-pad-button', mouse_button]
            if texture_validation is not None:
                command += ['--texture-validation', texture_validation]
            if attachment_validation is not None:
                command += ['--attachment-validation', attachment_validation]
            if user_directory is not None:
                command += ["--user-dir", str(user_directory)]
            if modules is not None:
                module_directory = Path(directory) / '模块 inputs'
                module_directory.mkdir()
                for name, contents in modules.items():
                    (module_directory / name).write_bytes(contents)
                command += ['--module-dir', str(module_directory)]
            for option, value in (('--app-dir', app_directory), ('--patch-dir', patch_directory), ('--addon-dir', addons_directory)):
                if value is not None:
                    command += [option, str(value)]
            return subprocess.run(command, capture_output=True, text=True, timeout=timeout, cwd=ROOT)

    def test_diagnostic_taps_are_entry_only_bounded_and_default_off(self):
        data = synthetic_probe(0)
        normal = self.invoke(data)
        self.assertNotIn('native_pad_diagnostic_taps', normal.stdout)
        for mode, value in (('--validate', '120'), ('--trace-enter', '120'), ('--enter', '-1'),
                            ('--enter', '501'), ('--enter', '1x'), ('--enter', '')):
            with self.subTest(mode=mode, value=value):
                result = self.invoke(data, mode, diagnostic_tap=value)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertNotIn('"event":"enter_game"', result.stdout)

    def test_diagnostic_circle_hold_is_entry_only_bounded_and_default_off(self):
        normal = self.invoke(exit_fixture(), '--enter')
        self.assertEqual(normal.returncode, 7, normal.stdout + normal.stderr)
        self.assertNotIn('native_pad_diagnostic_circle_hold', normal.stdout)
        for mode, value in (('--validate', '1500'), ('--trace-enter', '1500'), ('--enter', '-1'),
                            ('--enter', '2001'), ('--enter', '1x'), ('--enter', '')):
            with self.subTest(mode=mode, value=value):
                result = self.invoke(exit_fixture(), mode, diagnostic_circle_hold=value)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertNotIn('"event":"enter_game"', result.stdout)
        for value in (0, 1500, 2000):
            with self.subTest(valid=value):
                result = self.invoke(exit_fixture(), '--enter', diagnostic_circle_hold=value)
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
                events = [json.loads(line) for line in result.stdout.splitlines()]
                event = next(e for e in events if e['event'] == 'native_pad_diagnostic_circle_hold')
                self.assertEqual(event, dict(event='native_pad_diagnostic_circle_hold', trigger='H', milliseconds=value,
                                             changes_game_state_directly=False))

    def test_resident_gpu_requires_explicit_entry_mode_and_absolute_existing_library(self):
        data = synthetic_probe(0)
        for mode, path in (('--validate', 'missing.dll'), ('--trace-enter', 'missing.dll'),
                           ('--enter', 'missing.dll')):
            with self.subTest(mode=mode, path=path):
                result = self.invoke(data, mode, gpu_runtime=path)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertNotIn('"event":"enter_game"', result.stdout)

    def test_content_native_overlay_metadata_mounts_keys_and_read_only_lifetimes(self):
        with tempfile.TemporaryDirectory(prefix='kt-content-') as directory:
            app, patch, addons = content_tree(directory)
            before = {str(path): hashlib.sha256(path.read_bytes()).digest() for path in Path(directory).rglob('*') if path.is_file()}
            result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--content-self-test',
                                     str(app), str(patch), str(addons)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            cases = [event for event in events if event['event'] == 'content_self_test_case']
            self.assertEqual(len(cases), 23)
            self.assertTrue(all(event['passed'] for event in cases))
            self.assertEqual(before, {str(path): hashlib.sha256(path.read_bytes()).digest() for path in Path(directory).rglob('*') if path.is_file()})

    def test_content_builtin_dlc_sysv_default_pair_no_mount_and_readonly(self):
        with tempfile.TemporaryDirectory(prefix='kt-builtin-') as directory:
            app, patch, addons = content_tree(directory, title_id='CUSA09062',
                content_id='JP0092-CUSA09062_00-KETSUIDEATHTINY0')
            before = {str(p): hashlib.sha256(p.read_bytes()).digest() for p in Path(directory).rglob('*') if p.is_file()}
            result = self.invoke(content_fixture(built_in=True), '--enter', app_directory=app, patch_directory=patch)
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            entries = [e for e in events if e['event'] == 'native_content_entry']
            self.assertEqual([(e['label'], e['status'], e['built_in']) for e in entries],
                [('KETSUIBUILTIN001', 0, True), ('KETSUIBUILTIN002', 0, True)])
            delivered = [e for e in events if e['event'] == 'native_content_capability_delivered']
            self.assertEqual(len(delivered), 2)
            self.assertTrue(all(e['built_in'] and e['status'] == 0 for e in delivered))
            mixed = self.invoke(exit_fixture(), '--enter', app_directory=app, patch_directory=patch, addons_directory=addons)
            self.assertEqual(mixed.returncode, 7, mixed.stdout + mixed.stderr)
            entries = [e for line in mixed.stdout.splitlines() if (e := json.loads(line))['event'] == 'native_content_entry']
            self.assertEqual([e['status'] for e in entries], [4, 4, 0, 0])
            self.assertEqual(before, {str(p): hashlib.sha256(p.read_bytes()).digest() for p in Path(directory).rglob('*') if p.is_file()})

    def test_content_builtin_dlc_requires_exact_application_identity(self):
        for title, content in (('CUSA00001', 'XX0000-CUSA00001_00-BASEGAME00000000'),
                               ('CUSA09062', 'JP0092-CUSA09062_00-KETSUIDEATHTINY9')):
            with self.subTest(content=content), tempfile.TemporaryDirectory(prefix='kt-no-builtin-') as directory:
                app, patch, _ = content_tree(directory, title_id=title, content_id=content)
                result = self.invoke(content_fixture(empty_addons=True), '--enter', app_directory=app, patch_directory=patch)
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
                self.assertNotIn('native_content_entry', result.stdout)

    def test_content_builtin_dlc_coexists_with_64_installed_and_rejects_reserved_collision(self):
        with tempfile.TemporaryDirectory(prefix='kt-builtin-capacity-') as directory:
            app, patch, addons = content_tree(directory, title_id='CUSA09062',
                content_id='JP0092-CUSA09062_00-KETSUIDEATHTINY0')
            for index in range(3, 65):
                folder = addons / str(index) / 'sce_sys'
                folder.mkdir(parents=True)
                (folder / 'param.sfo').write_bytes(sfo_document(dict(TITLE_ID='CUSA09062', CATEGORY='ac',
                    CONTENT_ID=f'JP0092-CUSA09062_00-A{index:015d}')))
            result = self.invoke(exit_fixture(), '--enter', app_directory=app, patch_directory=patch, addons_directory=addons)
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            entries = [e for line in result.stdout.splitlines() if (e := json.loads(line))['event'] == 'native_content_entry']
            self.assertEqual(len(entries), 66)
            self.assertEqual(sum(e['status'] == 4 for e in entries), 64)
            (addons / '1/sce_sys/param.sfo').write_bytes(sfo_document(dict(TITLE_ID='CUSA09062', CATEGORY='ac',
                CONTENT_ID='JP0092-CUSA09062_00-KETSUIBUILTIN001')))
            collision = self.invoke(exit_fixture(), '--enter', app_directory=app, patch_directory=patch, addons_directory=addons)
            self.assertEqual(collision.returncode, 2, collision.stdout + collision.stderr)
            self.assertIn('reserved built-in label', collision.stderr)

    def test_file_host_readonly_overlay_seek_stat_errors_and_parallel_reads(self):
        with tempfile.TemporaryDirectory(prefix='kt-files-') as directory:
            app, patch, _ = content_tree(directory)
            before = {str(path): hashlib.sha256(path.read_bytes()).digest() for path in Path(directory).rglob('*') if path.is_file()}
            result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--file-self-test', str(app), str(patch)],
                                    capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            cases = [event for event in events if event['event'] == 'file_self_test_case']
            self.assertEqual(len(cases), 22)
            self.assertTrue(all(event['passed'] for event in cases))
            self.assertEqual(sum(event['event'] == 'native_file_request' for event in events), 128)
            summary = next(event for event in events if event['event'] == 'native_file_diagnostics')
            self.assertEqual(summary['request_details_omitted'], summary['requests'] - 128)
            self.assertGreater(summary['request_details_omitted'], 1000)
            self.assertEqual(sum(event['event'] == 'native_file_read' for event in events), 64)
            self.assertEqual(summary['read_details_omitted'], summary['successful_reads'] - 64)
            self.assertGreater(summary['read_details_omitted'], 0)
            self.assertEqual(before, {str(path): hashlib.sha256(path.read_bytes()).digest() for path in Path(directory).rglob('*') if path.is_file()})

    def test_libc_byte_search_and_target_c_locale_tables(self):
        result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--libc-memory-self-test'], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        checks = [e for e in events if e['event'] == 'libc_memory_self_test_case']
        self.assertEqual(len(checks), 34)
        self.assertTrue(all(e['passed'] for e in checks))

    def test_atof_sysv_double_return_range_errno_and_exact_namespace(self):
        result = self.invoke(atof_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        for library, module in (('fixture', 'libc'), ('libc', 'fixture')):
            result = self.invoke(atof_fixture(library=library, module=module), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertIn('unimplemented_import', result.stdout)

    def test_strtof_sysv_float_rounding_end_pointer_errno_and_namespace(self):
        result = self.invoke(strtof_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        for library, module in (('fixture', 'libc'), ('libc', 'fixture')):
            result = self.invoke(strtof_fixture(library=library, module=module), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'strtof')

    def test_qsort_sysv_guest_callback_empty_extents_overflow_and_namespace(self):
        for mode in ('normal', 'empty', 'one', 'zero_width'):
            result = self.invoke(qsort_fixture(mode=mode), '--enter')
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        result = self.invoke(qsort_fixture(mode='overflow'), '--enter')
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'qsort_invalid_extent_or_comparator')
        for library, module in (('fixture', 'libc'), ('libc', 'fixture')):
            result = self.invoke(qsort_fixture(library=library, module=module), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'qsort')

    def test_local_sixteen_bit_multibyte_conversion_boundaries_and_state(self):
        result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--text-self-test'],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        checks = [event for event in events if event['event'] == 'text_self_test_case']
        self.assertEqual(len(checks), 29)
        self.assertTrue(all(event['passed'] for event in checks))

    def test_text_sysv_wide_units_queries_pointer_updates_tails_and_guest_eilseq(self):
        result = self.invoke(text_conversion_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_text_conversion_requires_exact_libc_namespace(self):
        for library, module in (('libkernel', 'libc'), ('libc', 'libkernel')):
            result = self.invoke(text_conversion_fixture(library=library, module=module), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertIn('mbsrtowcs', result.stdout)

    def test_native_run_inspection_counts_absent_events_and_preserves_ui_evidence(self):
        with tempfile.TemporaryDirectory(prefix='kt-report-') as directory:
            root = Path(directory)
            (root / 'binary').mkdir()
            (root / 'binary/build.json').write_text(json.dumps({'buildId': 'synthetic-report'}))
            (root / 'run.json').write_text(json.dumps({'runId': 'synthetic-run', 'nativeBuildId': 'synthetic-report',
                'exitCode': 20, 'timedOut': False, 'observationLimitSeconds': 15}))
            (root / 'verification.json').write_text(json.dumps({'runId': 'synthetic-run',
                'uiVerification': {'menuVerified': False, 'evidence': 'synthetic sentinel'}}))
            (root / 'stdout.jsonl').write_text('\n'.join((
                json.dumps({'event': 'native_graphics_present_completed'}),
                json.dumps({'event': 'native_fios_read', 'actual': 7, 'result': 0}),
                'not-json}', '{"event":"partial"')))
            command = ['pwsh', '-NoProfile', '-File', str(ROOT / 'scripts/Inspect-NativeRun.ps1'),
                       '-ReportDirectory', str(root)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            output = json.loads((root / 'verification.json').read_text(encoding='utf-8-sig'))
            self.assertEqual(output['completeJsonLines'], 2)
            self.assertEqual(output['invalidJsonLineNumbers'], [3])
            self.assertEqual(output['incompleteLineNumbers'], [4])
            self.assertEqual((output['drawsCompleted'], output['submitsCompleted'], output['presentCompleted']), (0, 0, 1))
            self.assertEqual((output['loggedFiosReads'], output['loggedFiosReadBytes'], output['fioReadErrorCount']), (1, 7, 0))
            self.assertEqual((output['loggedSaveWriteCount'], output['loggedSaveWriteBytes'], output['largestCompletedSubmitDraws']), (0, 0, 0))
            self.assertEqual((output['loggedKernelReadCount'], output['loggedKernelReadBytes']), (0, 0))
            for field in ('mediaStartups', 'avPlayerInitializations', 'directMemoryErrors', 'directCapacityFailures',
                          'avPlayerSources', 'avPlayerPlayback', 'avPlayerEvents', 'avPlayerFrameSamples',
                          'avPlayerNaturalEnds', 'avPlayerCloses', 'avPlayerLoops', 'avPlayerFailures',
                          'avPlayerAllocationFailures', 'ngs2Waveforms', 'ngs2WaveformFrontiers',
                          'ngs2WaveformFailures', 'gpuTextureFrontiers', 'padWindowEdges', 'padButtonReads',
                          'windowCloseRequests', 'presentationShutdownBegins',
                          'presentationShutdownCompletions', 'shutdownIncompleteEvents'):
                self.assertEqual(output[field], [])
            self.assertEqual(output['unimplementedEvents'], [])
            self.assertEqual(output['uiVerification'], {'menuVerified': False, 'evidence': 'synthetic sentinel'})
            self.assertFalse(output['observationInProgress'])
            (root / 'stdout.jsonl').write_text('\n'.join(json.dumps(event) for event in (
                {'event': 'native_file_write', 'written': 9}, {'event': 'native_file_read', 'received': 12},
                {'event': 'native_media_startup', 'hresult': 0}, {'event': 'native_avplayer_init', 'source_count': 0},
                {'event': 'native_avplayer_source_ready', 'streams': 2},
                {'event': 'native_avplayer_playback', 'state': 3},
                {'event': 'native_avplayer_event', 'id': 1},
                {'event': 'native_avplayer_frame', 'kind': 'video', 'count': 120},
                {'event': 'native_avplayer_natural_end', 'video_frames': 123, 'audio_packets': 37},
                {'event': 'native_avplayer_close', 'source_count': 1},
                {'event': 'native_avplayer_loop', 'generation': 2},
                {'event': 'native_avplayer_frame_failure', 'hresult': -1},
                {'event': 'native_avplayer_frame_allocation_failure', 'bytes': 4096},
                {'event': 'native_ngs2_waveform_parsed', 'codec': 'hevag', 'samples': 56},
                {'event': 'native_ngs2_waveform_frontier', 'captured_bytes': 32},
                {'event': 'native_ngs2_hevag_decode_failure', 'reason': 'authored sentinel'},
                {'event': 'native_gpu_texture_frontier', 'descriptor': [1, 2, 3]},
                {'event': 'native_pad_window_edge', 'binding': 8, 'down': True},
                {'event': 'native_pad_read_buttons', 'buttons': 8},
                {'event': 'native_video_window_close_requested', 'source': 'WM_CLOSE'},
                {'event': 'native_presentation_shutdown_begin', 'handle': 1},
                {'event': 'native_presentation_shutdown_completed', 'handle': 1,
                 'window_thread_joined': True, 'resident_gpu_context_destroyed': False},
                {'event': 'native_shutdown_incomplete', 'reason': 'window_close_requested', 'stage': 'present',
                 'host_cleanup_complete': False, 'guest_finalizers_run': False,
                 'presentation_cleanup_complete': True, 'exit_code': 27},
                {'event': 'direct_memory_error', 'result': '0x80020023'},
                {'event': 'direct_memory_capacity_failure', 'allocated_bytes': 17})))
            result = subprocess.run(command, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            output = json.loads((root / 'verification.json').read_text(encoding='utf-8-sig'))
            self.assertEqual((output['loggedSaveWriteCount'], output['loggedSaveWriteBytes']), (1, 9))
            self.assertEqual((output['loggedKernelReadCount'], output['loggedKernelReadBytes']), (1, 12))
            self.assertEqual(output['mediaStartups'][0]['hresult'], 0)
            self.assertEqual(output['avPlayerInitializations'][0]['source_count'], 0)
            self.assertEqual(output['avPlayerSources'][0]['streams'], 2)
            self.assertEqual(output['avPlayerPlayback'][0]['state'], 3)
            self.assertEqual(output['avPlayerEvents'][0]['id'], 1)
            self.assertEqual(output['avPlayerFrameSamples'][0]['count'], 120)
            self.assertEqual(output['avPlayerNaturalEnds'][0]['video_frames'], 123)
            self.assertEqual(output['avPlayerCloses'][0]['source_count'], 1)
            self.assertEqual(output['avPlayerLoops'][0]['generation'], 2)
            self.assertEqual(output['avPlayerFailures'][0]['hresult'], -1)
            self.assertEqual(output['avPlayerAllocationFailures'][0]['bytes'], 4096)
            self.assertEqual(output['ngs2Waveforms'][0]['samples'], 56)
            self.assertEqual(output['ngs2WaveformFrontiers'][0]['captured_bytes'], 32)
            self.assertEqual(output['ngs2WaveformFailures'][0]['reason'], 'authored sentinel')
            self.assertEqual(output['gpuTextureFrontiers'][0]['descriptor'], [1, 2, 3])
            self.assertEqual(output['padWindowEdges'][0]['binding'], 8)
            self.assertEqual(output['padButtonReads'][0]['buttons'], 8)
            self.assertEqual(output['windowCloseRequests'][0]['source'], 'WM_CLOSE')
            self.assertEqual(output['presentationShutdownBegins'][0]['handle'], 1)
            self.assertTrue(output['presentationShutdownCompletions'][0]['window_thread_joined'])
            self.assertFalse(output['presentationShutdownCompletions'][0]['resident_gpu_context_destroyed'])
            self.assertEqual(output['shutdownIncompleteEvents'][0]['reason'], 'window_close_requested')
            self.assertTrue(output['shutdownIncompleteEvents'][0]['presentation_cleanup_complete'])
            self.assertFalse(output['shutdownIncompleteEvents'][0]['host_cleanup_complete'])
            self.assertEqual(output['shutdownIncompleteEvents'][0]['exit_code'], 27)
            self.assertEqual(output['unimplementedEvents'], [])
            self.assertEqual(output['directCapacityFailures'][0]['allocated_bytes'], 17)
            self.assertEqual(output['directMemoryErrors'][0]['result'], '0x80020023')
            self.assertFalse(output['uiVerification']['menuVerified'])
            before = (root / 'verification.json').read_bytes()
            (root / 'binary/build.json').write_text(json.dumps({'buildId': 'different-build'}))
            result = subprocess.run(command, capture_output=True, text=True, timeout=20)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual((root / 'verification.json').read_bytes(), before)

    def test_guest_setjmp_longjmp_restore_sysv_state_and_float_control(self):
        for value in (0, 37, -17):
            result = self.invoke(jump_fixture(value), '--enter')
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_guest_longjmp_nonempty_signal_mask_and_namespace_are_explicit(self):
        result = self.invoke(jump_fixture(signal_mask=True), '--enter')
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'longjmp_guest_signal_mask')
        result = self.invoke(jump_fixture(library='notLibc'), '--enter')
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'setjmp')

    def test_event_flags_real_wait_clear_cancel_delete_and_single_waiter_repeated(self):
        for _ in range(3):
            result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--event-flag-self-test'], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            checks = [e for e in events if e['event'] == 'event_flag_self_test_case']
            self.assertEqual(len(checks), 15)
            self.assertTrue(all(e['passed'] for e in checks))

    def test_fios_async_storage_lifecycle_callback_reentry_and_readonly_files_repeated(self):
        for _ in range(3):
            with tempfile.TemporaryDirectory(prefix='kt-fios-') as directory:
                app, patch, _ = content_tree(directory)
                before = {str(p): hashlib.sha256(p.read_bytes()).digest() for p in Path(directory).rglob('*') if p.is_file()}
                result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--fios-self-test', str(app), str(patch)],
                                        capture_output=True, text=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                events = [json.loads(line) for line in result.stdout.splitlines()]
                checks = [e for e in events if e['event'] == 'fios_self_test_case']
                self.assertEqual(len(checks), 25)
                self.assertTrue(all(e['passed'] for e in checks))
                self.assertEqual(before, {str(p): hashlib.sha256(p.read_bytes()).digest() for p in Path(directory).rglob('*') if p.is_file()})

    def test_file_sysv_all_eight_kernel_interfaces_and_sign_extended_read_error(self):
        with tempfile.TemporaryDirectory(prefix='kt-files-') as directory:
            app, patch, _ = content_tree(directory)
            result = self.invoke(file_fixture(), '--enter', app_directory=app, patch_directory=patch)
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_content_read_overlays_apply_to_read_pread_and_seek_without_original_writes(self):
        from game_fixes import encode_read_patches, digest
        with tempfile.TemporaryDirectory(prefix='kt-read-overlay-') as directory:
            app, patch, _ = content_tree(directory)
            before = (patch / 'overlay.txt').read_bytes()
            entries = [dict(path='overlay.txt', file_size=5, offset=i,
                            expected_sha256=digest(before[i:i+1]), replacement=bytes([value]))
                       for i, value in ((0, ord('P')), (4, ord('H')))]
            fixes = Path(directory) / '自制.ktfix'
            fixes.write_bytes(encode_read_patches('CUSA00001', '01.02', entries))
            result = self.invoke(file_fixture(read_expected=b'PatcH'), '--enter', app_directory=app,
                                 patch_directory=patch, read_patches=fixes)
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            self.assertIn('"read_patch_count":2', result.stdout)
            applied = [json.loads(line) for line in result.stdout.splitlines()
                       if line.startswith('{"event":"native_content_read_overlay_applied"')]
            self.assertEqual([(e['archive_bytes'], e['patch_offset'], e['patch_bytes']) for e in applied],
                             [(5, 0, 1), (5, 4, 1)])
            self.assertEqual((patch / 'overlay.txt').read_bytes(), before)
            self.assertEqual((app / 'overlay.txt').read_bytes(), b'base')

    def test_content_read_overlays_reject_bad_bytes_scope_missing_targets_and_truncation_before_entry(self):
        from game_fixes import encode_read_patches, digest
        with tempfile.TemporaryDirectory(prefix='kt-read-overlay-') as directory:
            app, patch, _ = content_tree(directory)
            entry = dict(path='overlay.txt', file_size=5, offset=0, expected_sha256=digest(b'p'), replacement=b'P')
            valid = encode_read_patches('CUSA00001', '01.02', [entry])
            cases = [valid[:-1], valid + b'\0', valid[:-1] + b'Q',
                     encode_read_patches('CUSA00002', '01.02', [entry]),
                     encode_read_patches('CUSA00001', '01.00', [entry]),
                     encode_read_patches('CUSA00001', '01.02', [dict(entry, path='absent')]),
                     encode_read_patches('CUSA00001', '01.02', [dict(entry, file_size=6)]),
                     encode_read_patches('CUSA00001', '01.02', [dict(entry, expected_sha256=digest(b'x'))])]
            fixes = Path(directory) / 'authored.ktfix'
            for index, data in enumerate(cases):
                with self.subTest(case=index):
                    fixes.write_bytes(data)
                    result = self.invoke(exit_fixture(), '--enter', app_directory=app,
                                         patch_directory=patch, read_patches=fixes)
                    self.assertNotEqual(result.returncode, 7, result.stdout + result.stderr)
                    self.assertNotIn('"event":"enter_game"', result.stdout)
                    self.assertEqual((patch / 'overlay.txt').read_bytes(), b'patch')

    def test_content_read_overlays_validate_without_guest_entry_and_require_content(self):
        from game_fixes import encode_read_patches, digest
        with tempfile.TemporaryDirectory(prefix='kt-read-overlay-') as directory:
            app, patch, _ = content_tree(directory)
            fixes = Path(directory) / 'authored.ktfix'
            fixes.write_bytes(encode_read_patches('CUSA00001', '01.02', [
                dict(path='overlay.txt', file_size=5, offset=0, expected_sha256=digest(b'p'), replacement=b'P')]))
            result = self.invoke(exit_fixture(), app_directory=app, patch_directory=patch, read_patches=fixes)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertNotIn('"event":"enter_game"', result.stdout)
            self.assertNotIn('"event":"native_content_read_overlay_applied"', result.stdout)
            for mode in ('--validate', '--trace-enter', '--binding-census'):
                result = self.invoke(exit_fixture(), mode, read_patches=fixes)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)

    def test_file_reachability_real_absence_directories_and_unicode(self):
        with tempfile.TemporaryDirectory(prefix='kt-files-') as directory:
            app, patch, _ = content_tree(directory)
            (app / '文件.txt').write_bytes(b'authored')
            for path, expected in (('/', 0), ('/app0', 0), ('/app0/', 0), ('/app0/sce_sys/', 0),
                                   ('/app0/文件.txt', 0), ('/app0/missing', 0x80020002),
                                   ('/app0/overlay.txt/', 0x80020014),
                                   ('/app0/overlay.txt/child', 0x80020014),
                                   ('/app0//', 0x80020016), ('/app0/../outside', 0x80020016),
                                   ('/app0/NUL', 0x80020016), ('/' + 'a' * 255, 0x8002003f)):
                with self.subTest(path=path):
                    result = self.invoke(file_fixture(action='reach', path=path, expected=expected), '--enter',
                                         app_directory=app, patch_directory=patch)
                    self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
                    for line in result.stdout.splitlines():
                        json.loads(line)

    def test_file_write_flags_never_modify_original_content(self):
        with tempfile.TemporaryDirectory(prefix='kt-files-') as directory:
            app, patch, _ = content_tree(directory)
            before = {str(path): path.read_bytes() for path in Path(directory).rglob('*') if path.is_file()}
            for flags in (1, 2, 0x200, 0x400, 0x601):
                result = self.invoke(file_fixture(action='open', flags=flags, expected=0x8002001e), '--enter',
                                     app_directory=app, patch_directory=patch)
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            self.assertEqual(before, {str(path): path.read_bytes() for path in Path(directory).rglob('*') if path.is_file()})

    def test_file_wrong_library_module_and_posix_contract_not_bound(self):
        with tempfile.TemporaryDirectory(prefix='kt-files-') as directory:
            app, patch, _ = content_tree(directory)
            for library, module in (('libScePosix', 'libkernel'), ('libkernel', 'libc')):
                result = self.invoke(file_fixture(action='reach', library=library, module=module), '--enter',
                                     app_directory=app, patch_directory=patch)
                self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
                self.assertIn('sceKernelCheckReachability', result.stdout)

    def test_file_unknown_namespace_is_explicit_not_fake_missing_file(self):
        with tempfile.TemporaryDirectory(prefix='kt-files-') as directory:
            app, patch, _ = content_tree(directory)
            result = self.invoke(file_fixture(action='reach', path='/download0/data'), '--enter',
                                 app_directory=app, patch_directory=patch)
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertIn('/download0/data', result.stdout)
            self.assertIn('native_file_host_or_namespace_failure', result.stdout)

    def test_unmounted_save_paths_do_not_fall_back_to_original_content(self):
        with tempfile.TemporaryDirectory(prefix='kt-files-') as directory:
            app, patch, _ = content_tree(directory)
            (app / 'data').write_bytes(b'not a save file')
            for path, expected in (('/savedata0/data', 0x80020002),
                                   ('/savedata15/data', 0x80020002),
                                   ('/savedata00/data', 0x80020016),
                                   ('/savedata16/data', 0x80020016),
                                   ('/savedata/data', 0x80020016)):
                with self.subTest(path=path):
                    result = self.invoke(file_fixture(action='reach', path=path, expected=expected), '--enter',
                                         app_directory=app, patch_directory=patch)
                    self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            self.assertEqual((app / 'data').read_bytes(), b'not a save file')

    def test_file_patch_non_directory_parent_shadows_base_subtree(self):
        with tempfile.TemporaryDirectory(prefix='kt-files-') as directory:
            app, patch, _ = content_tree(directory)
            (app / 'shadow').mkdir()
            (app / 'shadow/child').write_bytes(b'base')
            (patch / 'shadow').write_bytes(b'patch')
            result = self.invoke(file_fixture(action='reach', path='/app0/shadow/child', expected=0x80020014), '--enter',
                                 app_directory=app, patch_directory=patch)
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_file_invalid_utf8_rejected_before_log_serialization(self):
        with tempfile.TemporaryDirectory(prefix='kt-files-') as directory:
            app, patch, _ = content_tree(directory)
            result = self.invoke(file_fixture(action='reach', path=b'/app0/\xff', expected=0x80020016), '--enter',
                                 app_directory=app, patch_directory=patch)
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            for line in result.stdout.splitlines():
                json.loads(line)

    def test_file_reparse_parent_rejected_even_when_patch_leaf_missing(self):
        with tempfile.TemporaryDirectory(prefix='kt-files-') as directory:
            app, patch, _ = content_tree(directory)
            target = Path(directory) / 'outside'
            target.mkdir()
            (app / 'linked').mkdir()
            (app / 'linked/child').write_bytes(b'base must not be substituted')
            link = patch / 'linked'
            command = "New-Item -ItemType Junction -Path '{}' -Target '{}' | Out-Null".format(
                str(link).replace("'", "''"), str(target).replace("'", "''"))
            created = subprocess.run(['pwsh', '-NoProfile', '-Command', command], capture_output=True, text=True, timeout=10)
            self.assertEqual(created.returncode, 0, created.stdout + created.stderr)
            result = self.invoke(file_fixture(action='reach', path='/app0/linked/child'), '--enter',
                                 app_directory=app, patch_directory=patch)
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertIn('native_file_host_or_namespace_failure', result.stdout)
            self.assertTrue((app / 'linked/child').exists())

    def test_file_addon_file_and_directory_descriptors_pin_mount_until_close(self):
        with tempfile.TemporaryDirectory(prefix='kt-files-') as directory:
            app, patch, addons = content_tree(directory)
            for is_directory in (False, True):
                result = self.invoke(addon_file_fixture(directory=is_directory), '--enter', app_directory=app,
                                     patch_directory=patch, addons_directory=addons)
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_system_status_sysv_nine_fields_reserved_bytes_and_parameter_error(self):
        result = self.invoke(system_status_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_system_status_requires_exact_module_and_library(self):
        for library, module in (('libkernel', 'libSceSystemService'), ('libSceSystemService', 'libkernel')):
            result = self.invoke(system_status_fixture(library=library, module=module), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertIn('sceSystemServiceGetStatus', result.stdout)

    def test_pm4_decoder_writer_bounds_registers_clear_waits_and_malformed_packets(self):
        result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--pm4-self-test'],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        cases = [event for line in result.stdout.splitlines() if (event := json.loads(line))['event'] == 'pm4_self_test_case']
        self.assertEqual(len(cases), 41)
        self.assertTrue(all(event['passed'] for event in cases))

    def test_pm4_journal_cli_reads_owned_stream_without_game_or_gpu_execution(self):
        payloads = ([], [0x80000000], [0xc0055800, 0x82c40040, 0x200, 0, 0x12345678, 0, 10])
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / '命令 stream.bin'
            for words in payloads:
                with self.subTest(words=words):
                    raw = struct.pack(f'<{len(words)}I', *words)
                    source.write_bytes(raw)
                    result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--pm4-journal', str(source)],
                                            capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    event = json.loads(result.stdout)
                    self.assertEqual(event['event'], 'native_pm4_journal')
                    self.assertEqual(event['dwords'], len(words))
                    self.assertTrue(event['complete'])
                    self.assertFalse(event['gpu_executed'])
                    self.assertFalse(event['game_entry_reached'])
                    self.assertEqual(source.read_bytes(), raw)
                    if len(words) == 7:
                        self.assertEqual(event['last_acquire'], dict(control=0x82c40040, base=0x1234567800,
                                                                     bytes=0x20000, interval=10))

    def test_pm4_journal_cli_reports_exact_syntax_frontier_without_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'stream.bin'
            for words, code, offset in (([0x80000000, 0xc000ff00, 0x73], 4, 1),
                                        ([0xc0055800, 0x73], 1, 0),
                                        ([0xc0055800, 0x82c40041, 0, 0, 0, 0, 10], 7, 0)):
                with self.subTest(words=words):
                    raw = struct.pack(f'<{len(words)}I', *words)
                    source.write_bytes(raw)
                    result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--pm4-journal', str(source)],
                                            capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
                    event = json.loads(result.stdout)
                    self.assertEqual((event['error'], event['error_offset']), (code, offset))
                    self.assertEqual(event['error_words'], words[offset:offset + 16])
                    self.assertFalse(event['complete'])
                    self.assertFalse(event['gpu_executed'])
                    self.assertFalse(event['game_entry_reached'])
                    self.assertEqual(source.read_bytes(), raw)

    def test_pm4_journal_cli_rejects_bad_extent_missing_file_and_argument_count(self):
        executable = str(ROOT / 'out/native/KetsuiNativeProbe.exe')
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'stream.bin'
            commands = [[executable, '--pm4-journal'], [executable, '--pm4-journal', str(source), 'extra'],
                        [executable, '--pm4-journal', str(source)]]
            for command in commands:
                result = subprocess.run(command, capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertEqual(result.stdout, '')
            for raw in (b'123', bytes(65540)):
                source.write_bytes(raw)
                result = subprocess.run([executable, '--pm4-journal', str(source)], capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertEqual(result.stdout, '')
                self.assertEqual(source.read_bytes(), raw)

    def test_gnm_350_initialization_data_matches_pinned_reference(self):
        header = (ROOT / 'src/graphics/ps4_init_350.h').read_text(encoding='utf-8')
        body = header.split('kInit350{', 1)[1].split('};', 1)[0]
        words = [int(value, 0) for value in re.findall(r'\b(0x[0-9a-fA-F]+|\d+)u\b', body)]
        self.assertEqual(len(words), 124)
        self.assertEqual(hashlib.sha256(struct.pack('<124I', *words)).hexdigest(),
                         'fb4f24950a60bb9101387f4fac422e4810701ca48319030d9eda0e218e1ed593')

    def test_gnm_init_sysv_word_count_exact_extent_and_invalid_outputs(self):
        for mode in ('normal', 'large', 'short', 'null', 'unaligned'):
            result = self.invoke(gnm_init_fixture(mode=mode), '--enter')
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_gnm_init_requires_exact_driver_library_and_module(self):
        for library, module in (('libc', 'libSceGnmDriver'), ('libSceGnmDriver', 'libc')):
            result = self.invoke(gnm_init_fixture(library=library, module=module), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertIn('sceGnmDrawInitDefaultHardwareState350', result.stdout)

    def test_gnm_wait_flip_command_uses_live_port_and_rejects_closed_or_invalid_port(self):
        result = self.invoke(gnm_wait_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_roundf_sysv_float_ties_signed_zero_denormal_nan_and_infinity(self):
        for rounding in range(4):
            result = self.invoke(roundf_fixture(rounding=rounding), '--enter')
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_roundf_wrong_library_is_not_bound(self):
        result = self.invoke(roundf_fixture(library='libkernel'), '--enter')
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)

    def test_powf_sysv_sse_arguments_results_special_cases_and_preserved_errno(self):
        result = self.invoke(powf_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_powf_wrong_library_or_module_is_not_bound(self):
        for library, module in (('fixture', 'libc'), ('libc', 'fixture')):
            with self.subTest(library=library, module=module):
                result = self.invoke(synthetic_probe(identifier=nid('powf') + '#A#A', name='powf',
                                                     library=library, module=module), '--enter')
                self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
                self.assertEqual(json.loads(result.stdout.splitlines()[-1])['event'], 'unimplemented_import')

    def test_logarithm_sysv_double_integer_base_selector_and_preserved_errno(self):
        for selector in (0, 1, 2, -1, -2):
            with self.subTest(selector=selector):
                result = self.invoke(logarithm_fixture((selector,)), '--enter')
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_logarithm_wrong_library_or_module_is_not_bound(self):
        for name, library, module in ((name, library, module) for name in ('_Log', '_FLog')
                                      for library, module in (('fixture', 'libc'), ('libc', 'fixture'))):
            with self.subTest(name=name, library=library, module=module):
                result = self.invoke(synthetic_probe(identifier=nid(name) + '#A#A', name=name,
                                                     library=library, module=module), '--enter')
                self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
                self.assertEqual(json.loads(result.stdout.splitlines()[-1])['event'], 'unimplemented_import')

    def test_float_logarithm_sysv_base_selector_extremes_and_preserved_errno(self):
        for selector in (0, 1, 2, -1, -2):
            with self.subTest(selector=selector):
                result = self.invoke(logarithm_fixture((selector,), single=True), '--enter')
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_exp2_sysv_double_fractional_pitch_extremes_and_preserved_errno(self):
        result = self.invoke(exp2_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_exp2_wrong_library_or_module_is_not_bound(self):
        for library, module in (('fixture', 'libc'), ('libc', 'fixture')):
            with self.subTest(library=library, module=module):
                result = self.invoke(synthetic_probe(identifier=nid('exp2') + '#A#A', name='exp2',
                                                     library=library, module=module), '--enter')
                self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
                self.assertEqual(json.loads(result.stdout.splitlines()[-1])['event'], 'unimplemented_import')

    def test_remaining_imported_unary_math_sse_special_values_and_preserved_errno(self):
        definitions = (
            ('asinf', math.asin, (-1.0, -0.5, -0.0, 0.0, 0.5, 1.0, 2.0, math.inf, math.nan)),
            ('acosf', math.acos, (-1.0, -0.5, 0.0, 0.5, 1.0, 2.0, math.inf, math.nan)),
            ('atanf', math.atan, (-math.inf, -1.0, -0.0, 0.0, 1.0, math.inf, math.nan)),
            ('tanf', math.tan, (-1.0, -0.0, 0.0, 0.5, 1.0, math.inf, math.nan)),
            ('expf', math.exp, (-math.inf, -100.0, -1.0, 0.0, 1.0, 10.0, 100.0, math.inf, math.nan)),
            ('exp2f', math.exp2, (-math.inf, -149.0, -150.0, -1.0, 0.0, 0.5, 1.0, 128.0, math.inf, math.nan)),
        )
        for name, function, values in definitions:
            with self.subTest(name=name):
                cases = []
                for value in values:
                    try: expected = function(value)
                    except ValueError: expected = math.nan
                    except OverflowError: expected = math.inf
                    cases.append(((value,), expected))
                result = self.invoke(numerical_math_fixture(name, cases), '--enter')
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_remaining_imported_binary_math_sse_quadrants_remainders_and_double_power(self):
        remainders = (((5.5, 2.0), 1.5), ((-5.5, 2.0), -1.5), ((-0.0, 2.0), -0.0), ((0.0, 2.0), 0.0),
                      ((3.0, math.inf), 3.0), ((math.inf, 2.0), math.nan), ((1.0, 0.0), math.nan), ((math.nan, 2.0), math.nan))
        definitions = (
            ('fmodf', remainders, True), ('fmod', remainders, False),
            ('atan2f', tuple((args, math.atan2(*args)) for args in ((0.0, 1.0), (-0.0, 1.0), (1.0, 0.0), (-1.0, -1.0),
                                                                 (math.inf, math.inf), (-math.inf, math.inf), (math.nan, 1.0))), True),
            ('pow', (((-2.0, 3.0), -8.0), ((-2.0, 2.0), 4.0), ((2.0, -0.5), math.pow(2, -0.5)), ((2.0, 1024.0), math.inf),
                     ((2.0, -1075.0), 0.0), ((-2.0, 0.5), math.nan), ((math.nan, 0.0), 1.0),
                     ((1.0, math.nan), math.nan), ((1.0, math.inf), math.nan), ((-1.0, math.inf), math.nan)), False),
        )
        for name, cases, single in definitions:
            with self.subTest(name=name):
                result = self.invoke(numerical_math_fixture(name, cases, single=single), '--enter')
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_standard_imported_math_requires_exact_libc_namespace(self):
        for name in ('acosf', 'asinf', 'atan2f', 'atanf', 'tanf', 'exp2f', 'expf', 'fmod', 'fmodf', 'pow'):
            for library, module in (('fixture', 'libc'), ('libc', 'fixture')):
                with self.subTest(name=name, library=library, module=module):
                    result = self.invoke(synthetic_probe(identifier=nid(name) + '#A#A', name=name,
                                                         library=library, module=module), '--enter')
                    self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
                    self.assertEqual(json.loads(result.stdout.splitlines()[-1])['event'], 'unimplemented_import')

    def test_trig_sysv_float_double_and_nonzero_cosine_selector(self):
        for double in (False, True):
            result = self.invoke(trig_fixture(double=double), '--enter')
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_trig_wrong_library_is_not_bound(self):
        result = self.invoke(trig_fixture(library='libkernel'), '--enter')
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)

    def test_offline_trophy_does_not_publish_handle_or_modify_output(self):
        result = self.invoke(trophy_offline_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_offline_trophy_requires_exact_namespace(self):
        result = self.invoke(trophy_offline_fixture(library='libSceNpManager'), '--enter')
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)

    def trophy_run(self, app, user, phase):
        return subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--trophy-self-test', str(app), str(user), phase],
                              capture_output=True, text=True, timeout=10, cwd=ROOT)

    def trophy_fixture(self, directory, archive=None):
        app, _, _ = content_tree(directory)
        trophy = app / 'sce_sys/trophy/trophy00.trp'
        trophy.parent.mkdir(parents=True)
        trophy.write_bytes(trophy_archive() if archive is None else archive)
        return app, Path(directory) / '本地 user', trophy

    def test_trophy_local_lifecycle_unlocks_platinum_and_cross_process_readback(self):
        with tempfile.TemporaryDirectory(prefix='kt-trophy-') as directory:
            app, user, archive = self.trophy_fixture(directory)
            original = archive.read_bytes()
            for phase in ('write', 'read'):
                result = self.trophy_run(app, user, phase)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                events = [json.loads(line) for line in result.stdout.splitlines()]
                cases = [e for e in events if e['event'] == 'trophy_self_test_case']
                self.assertGreaterEqual(len(cases), 20)
                self.assertEqual(len(cases), len({e['name'] for e in cases}))
                self.assertTrue(all(e['passed'] for e in cases), result.stdout)
                self.assertEqual(events[-1], dict(event='trophy_self_test', read_back=phase == 'read', passed=True))
            self.assertEqual(archive.read_bytes(), original)
            journal = user / 'profiles/1000/CUSA00001/trophy/service-0/unlocks.bin'
            records = journal.read_bytes()
            self.assertEqual(len(records), 3 * 64)
            self.assertEqual([struct.unpack_from('<I', records, n * 64 + 40)[0] for n in range(3)], [1, 2, 0])

    def test_trophy_definition_rejects_encryption_invalid_xml_and_archive_bounds_without_publishing(self):
        invalid = [
            (trophy_archive(encrypted=True), 0x80551619),
            (trophy_archive(b'<!DOCTYPE trophyconf [<!ENTITY x SYSTEM "file:///C:/Windows/win.ini">]><trophyconf>&x;</trophyconf>'), 0x80551619),
            (trophy_archive(b'<trophyconf><trophy id="1" ttype="B"/><trophy id="1" ttype="G"/></trophyconf>'), 0x80551619),
            (trophy_archive(b'<trophyconf><trophy id="128" ttype="B"/></trophyconf>'), 0x80551619),
            (trophy_archive(b'<trophyconf><trophy id="1" ttype="B" pid="2"/></trophyconf>'), 0x80551619),
            (trophy_archive()[:-1], 0x80551616),
        ]
        for contents, expected in invalid:
            with self.subTest(expected=hex(expected), size=len(contents)), tempfile.TemporaryDirectory(prefix='kt-trophy-invalid-') as directory:
                app, user, archive = self.trophy_fixture(directory, contents)
                result = self.trophy_run(app, user, 'write')
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                events = [json.loads(line) for line in result.stdout.splitlines()]
                registration = next(e for e in events if e['event'] == 'trophy_register_test_result')
                self.assertEqual(registration['result'] & 0xffffffff, expected)
                self.assertNotIn('"event":"native_trophy_registered"', result.stdout)
                self.assertFalse(user.exists())
                self.assertEqual(archive.read_bytes(), contents)

    def test_trophy_journal_retains_commits_with_partial_tail_and_rejects_full_record_corruption(self):
        with tempfile.TemporaryDirectory(prefix='kt-trophy-journal-') as directory:
            app, user, _ = self.trophy_fixture(directory)
            written = self.trophy_run(app, user, 'write')
            self.assertEqual(written.returncode, 0, written.stdout + written.stderr)
            journal = user / 'profiles/1000/CUSA00001/trophy/service-0/unlocks.bin'
            committed = journal.read_bytes()
            journal.write_bytes(committed + b'partial')
            read = self.trophy_run(app, user, 'read')
            self.assertEqual(read.returncode, 0, read.stdout + read.stderr)
            self.assertIn('Incomplete journal tail ignored', read.stderr)
            self.assertEqual(journal.read_bytes(), committed + b'partial')
            corrupt = bytearray(committed)
            corrupt[48] ^= 1
            journal.write_bytes(corrupt)
            failed = self.trophy_run(app, user, 'read')
            self.assertEqual(failed.returncode, 1, failed.stdout + failed.stderr)
            self.assertIn(str(0x80551611 - 0x100000000), failed.stdout)
            self.assertEqual(journal.read_bytes(), corrupt)

    def test_display_safe_area_full_pc_output_preserves_reserved_bytes(self):
        result = self.invoke(safe_area_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_display_safe_area_requires_exact_namespace(self):
        for overrides in ({'library': 'libkernel'}, {'module': 'libkernel'}):
            result = self.invoke(safe_area_fixture(**overrides), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)

    def test_offline_socket_service_reports_encoded_unavailability(self):
        result = self.invoke(net_offline_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_offline_socket_service_requires_exact_namespace(self):
        for overrides in ({'library': 'libSceNpManager'}, {'module': 'libSceNpManager'}):
            result = self.invoke(net_offline_fixture(**overrides), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)

    def test_gnm_shader_sysv_descriptors_capacity_null_disable_and_invalid_inputs(self):
        for pixel in (False, True):
            for mode in ('normal', 'large', 'null', 'short', 'high_address', 'unaligned'):
                with self.subTest(pixel=pixel, mode=mode):
                    result = self.invoke(gnm_shader_fixture(pixel=pixel, mode=mode), '--enter')
                    self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_gnm_update_shader_sysv_descriptors_capacity_null_and_invalid_inputs(self):
        for pixel in (False, True):
            for mode in ('normal', 'large', 'null', 'short', 'high_address', 'unaligned'):
                with self.subTest(pixel=pixel, mode=mode):
                    result = self.invoke(gnm_shader_fixture(pixel=pixel, mode=mode, update=True), '--enter')
                    self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_gnm_update_shader_requires_exact_namespace(self):
        for pixel in (False, True):
            for overrides in ({'library': 'wrong', 'module': 'libSceGnmDriver'}, {'module': 'wrong'}):
                result = self.invoke(gnm_shader_fixture(pixel=pixel, update=True, **overrides), '--enter')
                self.assertEqual(result.returncode, 20, result.stdout + result.stderr)

    def test_gnm_draw_auto_sysv_packet_extent_and_flags(self):
        for flags in (0, 1, 2, 0xe0000001):
            result = self.invoke(gnm_draw_fixture(flags=flags), '--enter')
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_gnm_draft_observation_pins_bounded_direct_memory_only_once_and_releases_it(self):
        for mode in ('normal', 'claimed_short', 'distant'):
            result = self.invoke(gnm_draft_fixture(mode=mode), '--enter')
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            journals = [event for event in events if event['event'] == 'native_gnm_draft_journal']
            self.assertEqual(len(journals), 1 if mode == 'normal' else 0)
            if journals:
                self.assertTrue(journals[0]['complete'])
                self.assertEqual((journals[0]['dwords'], journals[0]['draws'], journals[0]['packets']), (263, 1, 47))
                self.assertFalse(journals[0]['gpu_executed'])

    def test_content_sysv_initialize_parameters_list_mount_key_and_unmount(self):
        with tempfile.TemporaryDirectory(prefix='kt-content-') as directory:
            app, patch, addons = content_tree(directory)
            result = self.invoke(content_fixture(), '--enter', app_directory=app, patch_directory=patch, addons_directory=addons)
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_content_without_additional_roots_reports_actual_empty_list(self):
        with tempfile.TemporaryDirectory(prefix='kt-content-') as directory:
            app, patch, _ = content_tree(directory)
            result = self.invoke(content_fixture(empty_addons=True), '--enter', app_directory=app, patch_directory=patch)
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            views = [json.loads(line) for line in result.stdout.splitlines() if 'native_content_view' in line]
            self.assertEqual(views[0]['additional_content_count'], 0)

    def test_content_namespace_includes_app_content_util_module(self):
        with tempfile.TemporaryDirectory(prefix='kt-content-') as directory:
            app, patch, addons = content_tree(directory)
            result = self.invoke(content_fixture(module='notAppContent'), '--enter', app_directory=app, patch_directory=patch, addons_directory=addons)
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'sceAppContentAppParamGetInt')

    def test_content_upgradable_application_does_not_fabricate_full_sku(self):
        with tempfile.TemporaryDirectory(prefix='kt-content-') as directory:
            app, patch, addons = content_tree(directory, app_type=2)
            result = self.invoke(content_fixture(), '--enter', app_directory=app, patch_directory=patch, addons_directory=addons)
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'sceAppContentAppParamGetInt_application_type')

    def test_content_metadata_bounds_types_duplicate_keys_and_utf8(self):
        with tempfile.TemporaryDirectory(prefix='kt-content-') as directory:
            app, _, _ = content_tree(directory)
            file = app / 'sce_sys/param.sfo'
            original = file.read_bytes()
            malformed = {}
            malformed['truncated_value_capacity'] = original[:-1]
            data = bytearray(original); struct.pack_into('<I', data, 16, 0xffffffff); malformed['entry_count_overflow'] = data
            data = bytearray(original); struct.pack_into('<H', data, 20, 0xffff); malformed['key_outside_table'] = data
            data = bytearray(original); struct.pack_into('<H', data, 22, 0x1234); malformed['unknown_value_format'] = data
            data = bytearray(original); struct.pack_into('<I', data, 20 + 12, 0xffffffff); malformed['value_offset_overflow'] = data
            data = sfo_document(dict(TITLE_ID='CUSA00001', CONTENT_ID='XX0000-CUSA00001_00-BASEGAME00000000', CATEGORY='gd', APP_TYPE=1, TEST_TEXT='ok'))
            data[struct.unpack_from('<I', data, 12)[0] + struct.unpack_from('<I', data, 20 + 4 * 16 + 12)[0]] = 0xff
            malformed['invalid_utf8_in_nonidentity_field'] = data
            malformed['duplicate_key'] = sfo_document([('TITLE_ID', 'CUSA00001'), ('TITLE_ID', 'CUSA00001')])
            malformed['embedded_nul'] = sfo_document(dict(TITLE_ID='CUSA\0' + '00001'))
            data = bytearray(original); struct.pack_into('<I', data, 20 + 3 * 16 + 4, 3); malformed['wrong_integer_length'] = data
            for label, raw in malformed.items():
                with self.subTest(label=label):
                    file.write_bytes(raw)
                    result = self.invoke(synthetic_probe(), app_directory=app)
                    self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                    self.assertNotIn('"event":"enter_game"', result.stdout)

    def test_content_patch_identity_and_category_must_match_base(self):
        with tempfile.TemporaryDirectory(prefix='kt-content-') as directory:
            app, patch, _ = content_tree(directory)
            file = patch / 'sce_sys/param.sfo'
            for fields in (dict(TITLE_ID='CUSA00002', CONTENT_ID='XX0000-CUSA00002_00-BASEGAME00000000', CATEGORY='gp'),
                           dict(TITLE_ID='CUSA00001', CONTENT_ID='XX0000-CUSA00001_00-BASEGAME00000000', CATEGORY='gd'),
                           dict(TITLE_ID='CUSA00001', CONTENT_ID='XX0000-CUSA00001_00-BASEGAME00000000', CATEGORY='gp', APP_TYPE=2)):
                with self.subTest(fields=fields):
                    file.write_bytes(sfo_document(fields))
                    result = self.invoke(synthetic_probe(), app_directory=app, patch_directory=patch)
                    self.assertEqual(result.returncode, 2, result.stdout + result.stderr)

    def test_content_original_roots_cannot_be_used_for_player_writes(self):
        with tempfile.TemporaryDirectory(prefix='kt-content-') as directory:
            app, _, _ = content_tree(directory)
            players = app / 'players'
            result = self.invoke(synthetic_probe(), app_directory=app, user_directory=players)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertFalse(players.exists())

    def test_content_reparse_roots_and_metadata_parents_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix='kt-content-') as directory:
            app, patch, _ = content_tree(directory)
            link = Path(directory) / 'root-junction'
            parent = Path(directory) / 'metadata-parent'
            parent.mkdir()
            for path, target in ((link, app), (parent / 'sce_sys', patch / 'sce_sys')):
                command = "New-Item -ItemType Junction -Path '{}' -Target '{}' | Out-Null".format(str(path).replace("'", "''"), str(target).replace("'", "''"))
                created = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', command], capture_output=True, text=True, timeout=10)
                self.assertEqual(created.returncode, 0, created.stdout + created.stderr)
            for selected_app, selected_patch in ((link, None), (app, parent)):
                result = self.invoke(synthetic_probe(), app_directory=selected_app, patch_directory=selected_patch)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)

    def test_module_late_function_copied_pointer_and_data_import_binding(self):
        result = self.invoke(module_main_fixture(late_imports=True), '--enter', modules={'fixture.prx.ktmodule': module_image_fixture()})
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        rebound = [json.loads(line) for line in result.stdout.splitlines() if 'native_import_rebind' in line]
        self.assertEqual(rebound[0]['functions'], 1)
        self.assertEqual(rebound[0]['data_slots'], 1)

    def test_module_source_is_checked_against_patch_first_resource_view(self):
        with tempfile.TemporaryDirectory(prefix='kt-content-') as directory:
            app, patch, _ = content_tree(directory)
            (app / 'fixture.prx').write_bytes(b'base source')
            raw = b'patch source'
            (patch / 'fixture.prx').write_bytes(raw)
            module = module_image_fixture()
            module[64:96] = hashlib.sha256(raw).digest()
            result = self.invoke(module_main_fixture(late_imports=True), '--enter', app_directory=app, patch_directory=patch,
                                 modules={'fixture.prx.ktmodule': module})
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            self.assertIn('native_module_source_verified', result.stdout)

    def test_module_prepared_from_wrong_layer_stops_before_initialization(self):
        with tempfile.TemporaryDirectory(prefix='kt-content-') as directory:
            app, patch, _ = content_tree(directory)
            raw = b'base source'
            (app / 'fixture.prx').write_bytes(raw)
            (patch / 'fixture.prx').write_bytes(b'patch source')
            module = module_image_fixture()
            module[64:96] = hashlib.sha256(raw).digest()
            result = self.invoke(module_main_fixture(), '--enter', app_directory=app, patch_directory=patch,
                                 modules={'fixture.prx.ktmodule': module})
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertNotIn('native_module_start', result.stdout)

    def test_module_existing_original_without_preparation_is_not_reported_missing(self):
        with tempfile.TemporaryDirectory(prefix='kt-content-') as directory:
            app, patch, _ = content_tree(directory)
            (patch / 'fixture.prx').write_bytes(b'original module requiring preparation')
            result = self.invoke(module_main_fixture(), '--enter', app_directory=app, patch_directory=patch, modules={})
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'native_module_preparation_required')

    def test_debug_child_receives_unicode_content_roots(self):
        with tempfile.TemporaryDirectory(prefix='kt-content-') as directory:
            app, patch, addons = content_tree(directory)
            result = self.invoke(content_fixture(), '--trace-enter', app_directory=app, patch_directory=patch, addons_directory=addons)
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_module_zero_init_relocation_shared_canary_exports_callbacks_and_duplicate_load(self):
        result = self.invoke(module_main_fixture(), '--enter', modules={'fixture.prx.ktmodule': module_image_fixture()})
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len([event for event in events if event['event'] == 'native_module_start']), 1)

    def test_prepared_prx_with_unused_weak_declaration_loads_and_runs_real_initializer(self):
        with tempfile.TemporaryDirectory(prefix='kt-prx-weak-') as directory:
            source = Path(directory) / 'simple.prx'
            source.write_bytes(synthetic_prx_elf(untyped_binding=2))
            packed, metadata = module_preparer().prepare(source, {}, module=True)
            result = self.invoke(prepared_module_load_fixture(), '--enter', modules={'simple.prx.ktmodule': packed})
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            starts = [e for e in events if e['event'] == 'native_module_start']
            self.assertEqual(len(starts), 1)
            self.assertEqual(len(metadata['omitted_unreferenced_weak_imports']), 1)

    def test_format_native_varargs_truncation_integers_and_gp_fp_spill(self):
        result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--format-self-test'],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        cases = [event for line in result.stdout.splitlines() if (event := json.loads(line))['event'] == 'format_self_test_case']
        self.assertEqual(len(cases), 33)
        self.assertTrue(all(event['passed'] for event in cases))

    def test_wide_format_sysv_unicode_lp64_fp_stack_truncation_and_va_list(self):
        result = self.invoke(wide_format_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_wide_format_requires_exact_libc_namespace(self):
        for library, module in (('libkernel', 'libc'), ('libc', 'libkernel')):
            result = self.invoke(wide_format_fixture(library=library, module=module), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertIn('swprintf', result.stdout)

    def test_wide_format_unknown_multibyte_field_stops_before_dereferencing_argument(self):
        result = self.invoke(wide_format_fixture(unsupported=True), '--enter')
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertIn('native_wide_format_multibyte_field', result.stdout)
        self.assertIn('0025 0073', result.stderr)

    def test_vsnprintf_sysv_va_list_and_returned_length(self):
        result = self.invoke(formatting_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_snprintf_sysv_register_capture(self):
        result = self.invoke(formatting_fixture(variadic=True), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_vsprintf_sysv_va_list(self):
        result = self.invoke(formatting_fixture(unbounded=True), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_sprintf_sysv_register_capture(self):
        result = self.invoke(formatting_fixture(variadic=True, unbounded=True), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_printf_guest_console_does_not_corrupt_json_stdout(self):
        result = self.invoke(printf_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        self.assertIn('[guest stdout] ok:-4294967297:1.2\n', result.stderr)
        self.assertTrue(all(isinstance(json.loads(line), dict) for line in result.stdout.splitlines()))

    def test_private_heap_aligned_allocation_lifetimes_and_concurrency(self):
        result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--heap-self-test'],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        cases = [event for line in result.stdout.splitlines() if (event := json.loads(line))['event'] == 'heap_self_test_case']
        self.assertEqual(len(cases), 10)
        self.assertTrue(all(event['passed'] for event in cases))

    def test_memalign_global_normalization_free_and_errno(self):
        result = self.invoke(aligned_heap_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    @requires_audio_device
    def test_audio_real_device_pacing_volume_and_close_cancellation(self):
        try:
            result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--audio-self-test'],
                                    capture_output=True, text=True, timeout=10)
        except subprocess.TimeoutExpired as error:
            def captured(value):
                return value.decode('utf-8', errors='replace') if isinstance(value, bytes) else value or ''
            self.fail('Audio self-test exceeded 10 seconds. Partial stdout:\n' + captured(error.stdout) +
                      '\nPartial stderr:\n' + captured(error.stderr))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        cases = [event for line in result.stdout.splitlines() if (event := json.loads(line))['event'] == 'audio_self_test_case']
        self.assertEqual(len(cases), 14)
        self.assertTrue(all(event['passed'] for event in cases))

        timings = [event for line in result.stdout.splitlines() if (event := json.loads(line))['event'] == 'native_audio_timing']
        self.assertTrue(timings)
        for event in timings:
            self.assertGreater(event['clock_us'], 0)
            self.assertLessEqual(event['written_frames'], event['submitted_frames'])
            self.assertLessEqual(event['consumed_frames'], event['written_frames'])
            self.assertLessEqual(event['padding'], event['device_capacity'])
            self.assertGreaterEqual(event['max_submit_wait_us'], event['submit_wait_us'])

    def test_pcm_voice_known_samples_mixing_resampling_loops_and_persistent_phase(self):
        result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--pcm-voice-self-test'],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        cases = [event for line in result.stdout.splitlines() if (event := json.loads(line))['event'] == 'pcm_voice_self_test_case']
        self.assertEqual(len(cases), 15)
        self.assertTrue(all(event['passed'] for event in cases))

    def test_ngs2_context_ownership_transaction_rollback_and_reentrant_callbacks(self):
        began = host_time.perf_counter_ns() // 1000
        result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--ngs2-self-test'],
                                capture_output=True, text=True, timeout=10)
        ended = host_time.perf_counter_ns() // 1000
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        cases = [event for line in result.stdout.splitlines() if (event := json.loads(line))['event'] == 'ngs2_self_test_case']
        self.assertEqual(len(cases), 78)
        self.assertTrue(all(event['passed'] for event in cases))
        names = {event['name'] for event in cases}
        self.assertIn('external_rack_lock_keeps_render_pending_until_unlock', names)
        self.assertIn('voice_control_under_game_stream_mutex_does_not_join_external_rack_lock_cycle', names)
        self.assertIn('pcm_blocks_ignore_unused_word_and_capture_owned_bounded_source_bytes', names)
        renders = [event for line in result.stdout.splitlines() if (event := json.loads(line))['event'] == 'native_ngs2_render_completed']
        self.assertTrue(renders)
        for event in renders:
            self.assertGreater(event['clock_us'], 0)
            self.assertGreaterEqual(event['render_us'], event['graph_wait_us'] + event['registry_wait_us'])
            self.assertGreaterEqual(event['starved_grains'], 0)
        locks = [json.loads(line) for line in result.stdout.splitlines()
                 if json.loads(line)['event'] == 'native_ngs2_rack_lock']
        starts = {e['lock_id']: e for e in locks if e['operation'] == 'lock' and e['depth'] == 1}
        finishes = {e['lock_id']: e for e in locks if e['operation'] == 'unlock' and e['depth'] == 0}
        self.assertTrue(starts)
        self.assertEqual(set(starts), set(finishes))
        for event in locks:
            self.assertGreaterEqual(event['clock_us'], began - 1000)
            self.assertLessEqual(event['clock_us'], ended + 1000)
            self.assertGreaterEqual(event['clock_us'], event['began_clock_us'])
        for identity, start in starts.items():
            finish = finishes[identity]
            self.assertEqual((start['rack'], start['thread'], start['began_clock_us']),
                             (finish['rack'], finish['thread'], finish['began_clock_us']))
            self.assertEqual(finish['held_us'], finish['clock_us'] - start['clock_us'])

    def test_ngs2_sysv_game_allocator_userdata_master_voice_and_cleanup(self):
        result = self.invoke(ngs2_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_ngs2_sysv_pcm_sampler_blocks_route_render_and_cleanup(self):
        result = self.invoke(ngs2_fixture(sampler=True), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertTrue(any(e['event'] == 'native_ngs2_render_completed' and e['active_sources'] == 1 and e['starved_sources'] == 0 for e in events))

    def test_ngs2_sysv_control_progresses_under_stream_mutex_while_rack_locked(self):
        for _ in range(3):
            result = self.invoke(ngs2_fixture(lock_control=True), '--enter')
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertTrue(any(event['event'] == 'native_thread_join' for event in events))

    def test_ngs2_allocator_callback_must_be_executable_game_code(self):
        result = self.invoke(ngs2_fixture(invalid_callback=True), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        self.assertNotIn('native_ngs2_system', result.stdout)

    def test_ngs2_wrong_namespace_does_not_bind(self):
        result = self.invoke(ngs2_fixture(library='notNgs2'), '--enter')
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'sceNgs2SystemCreateWithAllocator')

    def test_ngs2_unknown_voice_command_stops_instead_of_silent_success(self):
        result = self.invoke(ngs2_fixture(unknown_control=True), '--enter')
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        last = json.loads(result.stdout.splitlines()[-1])
        self.assertEqual(last['name'], 'sceNgs2VoiceControl_command')
        self.assertEqual(last['detail'], '0x7777')

    def test_thread_native_creation_sysv_entry_real_join_and_rollback(self):
        result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--thread-self-test'],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        cases = [event for event in events if event['event'] == 'thread_self_test_case']
        self.assertEqual(len(cases), 18)
        self.assertTrue(all(event['passed'] for event in cases))
        starts = [event for event in events if event['event'] == 'native_thread_start']
        self.assertEqual(len(starts), 22)
        self.assertEqual(starts[0]['requested_stack_bytes'], 131072)
        self.assertGreaterEqual(starts[0]['reserved_stack_bytes'], starts[0]['requested_stack_bytes'])

    def test_thread_sysv_five_arguments_entry_return_join_and_errno_isolation(self):
        result = self.invoke(thread_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(sum(event['event'] == 'native_thread_start' for event in events), 1)
        self.assertEqual(sum(event['event'] == 'native_thread_join' for event in events), 1)

    def test_thread_sysv_nonexecutable_callback_is_rejected_before_launch(self):
        result = self.invoke(thread_fixture(invalid_callback=True), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        self.assertNotIn('native_thread_start', result.stdout)

    def test_thread_wrong_namespace_is_not_bound(self):
        result = self.invoke(thread_fixture(library='notKernel'), '--enter')
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'scePthreadAttrInit')

    def test_condition_native_wait_notify_ownership_binding_and_destroy_repeated(self):
        for _ in range(3):
            result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--condition-self-test'],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            cases = [event for event in events if event['event'] == 'condition_self_test_case']
            self.assertEqual(len(cases), 19)
            self.assertTrue(all(event['passed'] for event in cases))

    def test_condition_sysv_cross_thread_wait_signal_reacquire_and_error_encoding(self):
        result = self.invoke(condition_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(sum(event.get('name') == 'scePthreadCondWait_begin' for event in events), 1)
        self.assertEqual(sum(event.get('name') == 'scePthreadCondWait_end' for event in events), 1)

    def test_condition_library_and_module_must_match_kernel(self):
        for library, module in (('libScePosix', 'libkernel'), ('libkernel', 'libc')):
            result = self.invoke(condition_fixture(library=library, module=module), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)

    def test_thread_scheduling_sysv_attribute_snapshot_and_native_priority(self):
        for mode, guest_priority, native_priority in ((0, 699, 1), (4, 700, 0)):
            result = self.invoke(thread_scheduling_fixture(inheritance=mode), '--enter')
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            started = next(event for event in events if event['event'] == 'native_thread_start')
            self.assertEqual((started['inheritance'], started['guest_priority'], started['native_priority']), (mode, guest_priority, native_priority))

    def test_thread_scheduling_requires_kernel_library_and_module(self):
        for library, module in (('libScePosix', 'libkernel'), ('libkernel', 'libc')):
            result = self.invoke(thread_scheduling_fixture(library=library, module=module), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'scePthreadAttrSetinheritsched')

    def test_gcn_binary_reader_authored_shaders_validation_and_bounded_memory_repeated(self):
        for _ in range(3):
            result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--shader-self-test'],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            cases = [event for event in events if event['event'] == 'shader_self_test_case']
            self.assertEqual(len(cases), 70)
            self.assertTrue(all(event['passed'] for event in cases))
            self.assertEqual(events[-1], {'event': 'shader_self_test', 'passed': True})

    def test_gnm_submit_nine_sysv_arguments_and_owned_snapshot_not_execution(self):
        result = self.invoke(gnm_submit_fixture(), '--enter')
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        submit = next(event for event in events if event['event'] == 'native_gnm_submit_inspect')
        self.assertEqual((submit['count'], submit['video'], submit['buffer'], submit['flip_mode'], submit['flip_argument']),
                         (1, 0x89abcdef, 0x12345678, 0x76543210, '0xfedcba9876543210'))
        self.assertFalse(submit['accepted'])
        self.assertFalse(submit['gpu_executed'])
        snapshot = next(event for event in events if event['event'] == 'native_gnm_snapshot')
        self.assertEqual((snapshot['kind'], snapshot['words']), ('dcb', [0x80000000]))
        self.assertEqual(events[-1]['name'], 'sceGnmSubmitAndFlipCommandBuffers_execution_pending')

    def test_gnm_submit_metadata_and_payload_bounds_are_diagnostic_not_success(self):
        for mode, count, frontier in (('bad_metadata', 1, 'unreadable_metadata'), ('unregistered', 1, 'unregistered_dcb'),
                                     ('gpu_only', 1, 'unregistered_dcb'), ('oversized', 1, 'inspection_extent'),
                                     ('unaligned_size', 1, 'inspection_extent'), ('normal', 0, 'inspection_count'),
                                     ('normal', 17, 'inspection_count'), ('ccb', 1, 'ccb_inspection_pending')):
            with self.subTest(mode=mode, count=count):
                result = self.invoke(gnm_submit_fixture(mode=mode, count=count), '--enter')
                self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
                events = [json.loads(line) for line in result.stdout.splitlines()]
                self.assertEqual(events[-1]['name'], 'native_gnm_submit_' + frontier)
                self.assertFalse(any(event['event'] == 'native_gnm_snapshot' for event in events))

    def test_gnm_invalid_stream_retains_exact_owned_words_before_stopping(self):
        packets = ([0xc0055800, 0x73, 1, 0, 0, 0, 10],
                   [0x80000000, 0xc0055800, 0x73],
                   [0x80000000, 0xc000ff00, 0x12345678])
        for payload in packets:
            with self.subTest(payload=payload):
                result = self.invoke(gnm_submit_fixture(payload=payload), '--enter')
                self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
                events = [json.loads(line) for line in result.stdout.splitlines()]
                saved = [event for event in events if event['event'] == 'native_gnm_snapshot' and
                         event['kind'] == 'invalid_dcb']
                self.assertEqual(len(saved), 1)
                self.assertEqual(saved[0]['words'], payload)
                self.assertEqual(saved[0]['dwords'], len(payload))
                self.assertFalse(saved[0]['gpu_executed'])
                self.assertEqual(events[-1]['name'], 'native_gnm_submit_invalid_stream')

    def test_gnm_large_owned_streams_cross_old_observation_limit_and_enforce_total_budget(self):
        for words, count in ((16384, 1), (17715, 1), (262144, 1), (262144, 4)):
            with self.subTest(words=words, count=count):
                # The largest case emits >11 MiB through unbuffered stdout.
                # Keep every word/content assertion; allow for slow log pipes.
                result = self.invoke(gnm_submit_fixture(padded_words=words, count=count), '--enter', timeout=30)
                self.assertEqual(result.returncode, 20, result.stderr)
                events = [json.loads(line) for line in result.stdout.splitlines()]
                self.assertEqual(events[-1]['name'], 'sceGnmSubmitAndFlipCommandBuffers_execution_pending')
                snapshots = [e for e in events if e['event'] == 'native_gnm_snapshot' and e['kind'] == 'dcb']
                self.assertEqual(len(snapshots), count)
                for snapshot in snapshots:
                    self.assertEqual(snapshot['dwords'], words)
                    self.assertEqual(snapshot['words'], [0x80000000] * words)
                journals = [e for e in events if e['event'] == 'native_gnm_submit_journal']
                self.assertEqual(len(journals), count)
                self.assertTrue(all(e['complete'] and e['packets'] == words for e in journals))
        for size, count, frontier in ((1048580, 1, 'inspection_extent'), (0, 1, 'inspection_extent'),
                                      (0xfffffffc, 1, 'inspection_extent'), (1048576, 5, 'inspection_budget')):
            with self.subTest(size=size, count=count):
                result = self.invoke(gnm_submit_fixture(declared_bytes=size, count=count), '--enter')
                self.assertEqual(result.returncode, 20, result.stderr)
                events = [json.loads(line) for line in result.stdout.splitlines()]
                self.assertEqual(events[-1]['name'], 'native_gnm_submit_' + frontier)
                self.assertFalse(any(e['event'] == 'native_gnm_snapshot' for e in events))

    def test_gnm_large_stream_tail_is_validated_and_snapshot_covers_exact_extent(self):
        result = self.invoke(gnm_submit_fixture(padded_words=17715, last_word=0xc000ff00), '--enter')
        self.assertEqual(result.returncode, 20, result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(events[-1]['name'], 'native_gnm_submit_invalid_stream')
        journal = next(e for e in events if e['event'] == 'native_gnm_submit_journal')
        self.assertEqual(journal['error_offset'], 17714)
        self.assertFalse(journal['complete'])
        saved = next(e for e in events if e['event'] == 'native_gnm_snapshot' and e['kind'] == 'invalid_dcb')
        self.assertEqual(saved['dwords'], 17715)
        self.assertEqual(saved['words'], [0x80000000] * 17714 + [0xc000ff00])

    def test_gnm_submit_inspection_count_boundary_and_exact_driver_namespace(self):
        result = self.invoke(gnm_submit_fixture(count=16), '--enter')
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(sum(event['event'] == 'native_gnm_snapshot' for event in events), 16)
        for library, module in (('libkernel', 'libSceGnmDriver'), ('libSceGnmDriver', 'libkernel')):
            result = self.invoke(gnm_submit_fixture(library=library, module=module), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'sceGnmSubmitAndFlipCommandBuffers')

    def test_semaphore_native_counts_waiters_timeouts_cancel_delete_and_identity_repeated(self):
        for _ in range(3):
            result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--semaphore-self-test'],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            cases = [event for event in events if event['event'] == 'semaphore_self_test_case']
            self.assertEqual(len(cases), 52)
            self.assertTrue(all(event['passed'] for event in cases))
            self.assertEqual(events[-1], {'event': 'semaphore_self_test', 'passed': True})

    def test_semaphore_sysv_six_args_qword_handles_timeouts_and_cross_thread_credit(self):
        result = self.invoke(semaphore_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        created = next(event for event in events if event['event'] == 'native_semaphore_create')
        self.assertGreater(created['handle'], 0xffffffff)
        self.assertEqual((created['name_bytes'], created['initial'], created['maximum']), ('AuthoredSemaphore', 0, 3))
        self.assertEqual(sum(event['event'] == 'native_thread_join' for event in events), 1)
        waits = [event for event in events if event['event'] == 'native_semaphore_wait_end']
        self.assertTrue(any(event['result'] == -2147352516 for event in waits))  # encoded ETIMEDOUT

    def test_semaphore_requires_exact_kernel_library_and_module(self):
        for library, module in (('libScePosix', 'libkernel'), ('libkernel', 'libc')):
            result = self.invoke(semaphore_fixture(library=library, module=module), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'sceKernelCreateSema')

    def test_semaphore_nonnull_options_stop_instead_of_being_ignored(self):
        result = self.invoke(semaphore_fixture(options=True), '--enter')
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'sceKernelCreateSema_options')
        self.assertNotIn('native_semaphore_create', result.stdout)

    def test_controlled_terminal_exit_blocks_concurrent_partial_stdout_writer(self):
        for _ in range(5):
            result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--terminal-log-self-test'],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertEqual(events[-1]['event'], 'unimplemented_api')
            self.assertEqual(events[-1]['name'], 'terminal_log_self_test')
            self.assertNotIn('late_writer', result.stdout)

    def test_known_window_close_has_incomplete_shutdown_record_not_api_frontier(self):
        for _ in range(5):
            result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--terminal-close-log-self-test'],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 27, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            final = events[-1]
            self.assertEqual(final['event'], 'native_shutdown_incomplete')
            self.assertEqual(final['reason'], 'window_close_requested')
            self.assertEqual(final['stage'], 'self_test')
            self.assertEqual(final['exit_code'], 27)
            self.assertFalse(final['guest_finalizers_run'])
            self.assertFalse(final['host_cleanup_complete'])
            self.assertTrue(final['presentation_cleanup_complete'])
            self.assertFalse(any(e['event'] == 'unimplemented_api' for e in events))
            self.assertNotIn('late_writer', result.stdout)

    def test_affinity_native_sparse_mapping_real_thread_setup_and_rollback(self):
        result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--thread-affinity-self-test'],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        cases = [event for event in events if event['event'] == 'thread_affinity_self_test_case']
        self.assertEqual(len(cases), 8)
        self.assertTrue(all(event['passed'] for event in cases))
        self.assertEqual(events[-1], {'event': 'thread_affinity_self_test', 'passed': True})

    def test_affinity_sysv_64bit_mask_validation_and_thread_attribute_snapshot(self):
        for mask in (1, 0x20, 0x3f):
            result = self.invoke(affinity_fixture(mask=mask), '--enter')
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            started = next(event for event in events if event['event'] == 'native_thread_start')
            self.assertEqual(started['guest_affinity'], mask)
            self.assertNotEqual(started['native_affinity'], 0)

    def test_affinity_requires_exact_kernel_library_and_module(self):
        for library, module in (('libScePosix', 'libkernel'), ('libkernel', 'libc')):
            result = self.invoke(affinity_fixture(library=library, module=module), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'scePthreadAttrSetaffinity')

    @requires_audio_device
    def test_audio_sysv_initialize_open_six_arguments_output_volume_and_close(self):
        result = self.invoke(audio_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_audio_wrong_namespace_is_not_resolved(self):
        result = self.invoke(audio_fixture(library='notAudio'), '--enter')
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'sceAudioOutInit')

    def test_audio_unimplemented_attributes_stop_explicitly(self):
        for format_code, name in ((0x20001, 'sceAudioOutOpen_attributes'), (0x10001, 'sceAudioOutOpen_attributes')):
            with self.subTest(format_code=format_code):
                result = self.invoke(audio_fixture(format_code=format_code), '--enter')
                self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
                self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], name)

    @requires_audio_device
    def test_audio_eight_channel_sysv_formats(self):
        for format_code in (2, 5, 6, 7):
            with self.subTest(format_code=format_code):
                result = self.invoke(audio_fixture(format_code=format_code), '--enter')
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_format_unsupported_conversion_diagnostic(self):
        result = self.invoke(formatting_fixture(unsupported=True), '--enter')
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        last = json.loads(result.stdout.splitlines()[-1])
        self.assertEqual(last['name'], 'native_format_conversion')
        self.assertEqual(last['caller_image'], 'eboot.bin')

    def test_format_wrong_namespace_is_not_resolved(self):
        result = self.invoke(formatting_fixture(library='notLibc'), '--enter')
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'vsnprintf')

    def test_module_trace_forwards_unicode_module_directory(self):
        result = self.invoke(module_main_fixture(), '--trace-enter', modules={'fixture.prx.ktmodule': module_image_fixture()})
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_module_missing_function_diagnostic_names_importing_image(self):
        result = self.invoke(module_main_fixture(), '--enter', modules={'fixture.prx.ktmodule': module_image_fixture(missing=True)})
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        last = json.loads(result.stdout.splitlines()[-1])
        self.assertEqual(last['caller_image'], 'fixture.prx')
        self.assertEqual(last['name'], 'ModuleMissing')

    def test_module_data_guard_diagnostic_names_importing_image(self):
        result = self.invoke(module_main_fixture(), '--enter', modules={'fixture.prx.ktmodule': module_image_fixture(data_fault=True)})
        self.assertEqual(result.returncode, 22, result.stdout + result.stderr)
        last = json.loads(result.stdout.splitlines()[-1])
        self.assertEqual(last['importing_image'], 'fixture.prx')

    def test_module_bad_metadata_is_not_started(self):
        module = module_image_fixture()
        struct.pack_into('<Q', module, 112, 4)
        result = self.invoke(module_main_fixture(), '--enter', modules={'fixture.prx.ktmodule': module})
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertNotIn('"event":"native_module_start"', result.stdout)

    def test_sysv_bridge(self):
        result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"), "--abi-self-test"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["result"], 91)

    def test_synthetic_image_validation(self):
        result = self.invoke(synthetic_probe())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('"game_entry_reached":false', result.stdout)

    def test_binding_census_uses_real_resolvers_and_exact_namespace_without_entry(self):
        for name, kind, library, module, expected in (
                ('atof', 2, 'libc', 'libc', 'host_bound'),
                ('atof', 2, 'libc', 'wrong-module', 'unbound'),
                ('atof', 2, 'wrong-library', 'libc', 'unbound'),
                ('__stack_chk_guard', 1, 'libkernel', 'libkernel', 'host_bound'),
                ('MissingData', 1, 'libkernel', 'libkernel', 'unbound')):
            with self.subTest(name=name, library=library, module=module):
                image = synthetic_probe(kind, nid(name) + '#A#A', name, library, module)
                image[128:130] = b'\x0f\x0b'  # Entry must not execute.
                result = self.invoke(image, '--binding-census')
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                events = [json.loads(line) for line in result.stdout.splitlines()]
                record = next(item for item in events if item['event'] == 'native_binding_census_import')
                self.assertEqual((record['name'], record['type'], record['status']), (name, kind, expected))
                self.assertEqual((record['library'], record['module']), (library, module))
                summary = events[-1]
                self.assertEqual(summary['imports'], 1)
                self.assertEqual(summary['unbound'], int(expected == 'unbound'))
                self.assertFalse(summary['game_entry_reached'])
                self.assertFalse(summary['module_initializers_called'])
                self.assertFalse(summary['capability_validation'])

    def test_binding_census_includes_late_function_and_data_exports_without_initializer(self):
        module = module_image_fixture(missing=True, data_fault=True)
        module[160:162] = b'\x0f\x0b'  # An accidental initializer call must fail.
        result = self.invoke(module_main_fixture(late_imports=True), '--binding-census',
                             modules={'fixture.prx.ktmodule': module, 'ignored.txt': b'not a module'})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        records = [item for item in events if item['event'] == 'native_binding_census_import']
        for name, kind in (('TestValue', 2), ('TestState', 1)):
            record = next(item for item in records if item['image_name'] == 'eboot.bin' and item['name'] == name)
            self.assertEqual((record['status'], record['type']), ('module_export_available', kind))
        for name in ('ModuleMissing', 'ModuleDataMissing'):
            record = next(item for item in records if item['name'] == name)
            self.assertEqual((record['image_name'], record['status']), ('fixture.prx', 'unbound'))
        self.assertEqual(events[-1]['images'], 2)
        self.assertEqual(events[-1]['module_export_available'], 2)
        self.assertEqual(events[-1]['unbound'], 2)
        self.assertNotIn('native_module_start', result.stdout)
        self.assertNotIn('"event":"enter_game"', result.stdout)

    def test_binding_census_rejects_bad_modules_and_does_not_create_profile(self):
        for modules in ({'bad.ktmodule': module_image_fixture()},
                        {'fixture.prx.ktmodule': b'truncated'},
                        {f'm{i}.prx.ktmodule': module_image_fixture() for i in range(17)}):
            result = self.invoke(synthetic_probe(), '--binding-census', modules=modules)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertNotIn('native_binding_census_complete', result.stdout)
        with tempfile.TemporaryDirectory(prefix='kt-census-') as directory:
            profile = Path(directory) / 'must-not-create'
            result = self.invoke(synthetic_probe(), '--binding-census', user_directory=profile)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertFalse(profile.exists())

    def test_function_import_trap(self):
        result = self.invoke(synthetic_probe(), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        event = json.loads(result.stdout.splitlines()[-1])
        self.assertEqual(event["event"], "unimplemented_import")
        self.assertEqual(event["name"], "synthetic_test")
        self.assertEqual(event["caller_offset"], "0xa")

    def test_function_import_trap_preserves_six_64_bit_arguments(self):
        asm = SyntheticCode()
        asm.labels["missing"] = 4112
        asm.emit("48 83 ec 08")
        expected = [0x1111111111111111 * index for index in range(1, 7)]
        for instruction, value in zip(("48 bf", "48 be", "48 ba", "48 b9", "49 b8", "49 b9"), expected):
            asm.emit(instruction + " " + struct.pack("<Q", value).hex(" "))
        asm.relative("ff 15", "missing")
        return_offset = len(asm.code)
        asm.emit("0f 0b")
        fixture = interfaces_fixture(asm.finish(), [("fixture_six_arguments", "fixture_library")])
        result = self.invoke(fixture, "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        event = json.loads(result.stdout.splitlines()[-1])
        self.assertEqual([int(value, 16) for value in event["arguments"]], expected)
        self.assertEqual(event["argument0"], event["arguments"][0])
        self.assertEqual(int(event["caller_offset"], 16), return_offset)

    def test_data_import_guard(self):
        result = self.invoke(synthetic_probe(1), "--enter")
        self.assertEqual(result.returncode, 22, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])["event"], "unimplemented_data_import")

    def test_truncated_probe_rejected(self):
        result = self.invoke(synthetic_probe()[:32])
        self.assertEqual(result.returncode, 2)

    def test_trailing_probe_data_rejected(self):
        result = self.invoke(synthetic_probe() + b"extra")
        self.assertEqual(result.returncode, 2)

    def test_invalid_relocation_rejected(self):
        data = synthetic_probe()
        struct.pack_into("<Q", data, len(data) - 32, 4096)
        result = self.invoke(data)
        self.assertEqual(result.returncode, 2)

    def test_entry_in_zero_fill_rejected(self):
        data = synthetic_probe()
        struct.pack_into("<Q", data, 16, 128)
        result = self.invoke(data)
        self.assertEqual(result.returncode, 2)

    def test_unknown_capability_rejected(self):
        data = synthetic_probe()
        struct.pack_into("<Q", data, 56, 2)
        result = self.invoke(data)
        self.assertEqual(result.returncode, 2)

    def test_atexit_lifo_and_status(self):
        result = self.invoke(exit_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(sum(e["event"] == "atexit_callback" for e in events), 2)
        self.assertEqual(events[-1], {"event": "native_exit", "status": 7})

    def test_immediate_exit_skips_callbacks(self):
        result = self.invoke(exit_fixture(immediate=True), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertFalse(any(e["event"] == "atexit_callback" for e in events))
        self.assertEqual(events[-1], {"event": "native_immediate_exit", "status": 7})

    def test_nonzero_stack_canary(self):
        result = self.invoke(canary_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_stack_canary_is_read_only(self):
        result = self.invoke(canary_fixture(write=True), "--enter")
        self.assertEqual(result.returncode, 22, result.stdout + result.stderr)
        self.assertIn('"event":"unhandled_exception"', result.stdout)
        self.assertNotIn('"event":"unimplemented_data_import"', result.stdout)

    def test_stack_check_failure_stops(self):
        result = self.invoke(synthetic_probe(identifier="Ou3iL1abvng#A#A", name="__stack_chk_fail",
                                              library="libkernel", module="libkernel"), "--enter")
        self.assertEqual(result.returncode, 23, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])["event"], "stack_check_failure")

    def test_new_delete_alignment_storage_and_zero_size(self):
        for array in (False, True):
            for nothrow in (False, True):
                with self.subTest(array=array, nothrow=nothrow):
                    result = self.invoke(allocation_fixture(array=array, nothrow=nothrow), "--enter")
                    self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_nothrow_allocation_failure_returns_null(self):
        for array in (False, True):
            with self.subTest(array=array):
                result = self.invoke(allocation_fixture("oom", array=array, nothrow=True), "--enter")
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_throwing_allocation_failure_stops_at_unsupported_exception(self):
        for array in (False, True):
            with self.subTest(array=array):
                result = self.invoke(allocation_fixture("oom", array=array), "--enter")
                self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
                self.assertEqual(json.loads(result.stdout.splitlines()[-1])["event"],
                                 "unimplemented_cpp_exception")

    def test_invalid_delete_stops_before_windows_heap(self):
        result = self.invoke(allocation_fixture("invalid"), "--enter")
        self.assertEqual(result.returncode, 24, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])["event"], "invalid_heap_pointer")

    def test_double_delete_stops(self):
        result = self.invoke(allocation_fixture("double"), "--enter")
        self.assertEqual(result.returncode, 24, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])["event"], "invalid_heap_pointer")

    def test_new_wrong_module_is_not_resolved(self):
        result = self.invoke(synthetic_probe(identifier=nid("_Znwm") + "#A#A", name="_Znwm",
                                             library="libc", module="fixture_module"), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])["event"], "unimplemented_import")

    def test_malloc_free_storage_and_failure(self):
        for mode in ("normal", "oom"):
            with self.subTest(mode=mode):
                result = self.invoke(allocation_fixture(mode, c_allocation=True), "--enter")
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_malloc_errno_contract(self):
        result = self.invoke(errno_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_mutex_sysv_lifecycle(self):
        result = self.invoke(mutex_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_mutex_native_contention_and_ownership(self):
        result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"), "--sync-self-test"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        cases = [e for e in events if e["event"] == "mutex_self_test_case"]
        self.assertEqual(len(cases), 8)
        self.assertTrue(all(e["passed"] for e in cases))
        self.assertEqual(events[-1], {"event": "mutex_self_test", "passed": True})

    def test_mutex_wrong_namespace_is_not_resolved(self):
        result = self.invoke(synthetic_probe(identifier=nid("scePthreadMutexattrInit") + "#A#A",
                                             name="scePthreadMutexattrInit", library="fixture_library",
                                             module="libkernel"), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])["event"], "unimplemented_import")

    def test_dialog_module_sysv_lifecycle(self):
        result = self.invoke(dialog_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_unknown_system_module_stops_instead_of_reporting_loaded(self):
        result = self.invoke(dialog_fixture(unknown_module=True), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        event = json.loads(result.stdout.splitlines()[-1])
        self.assertEqual(event["event"], "unimplemented_api")
        self.assertEqual(event["detail"], "0x7777")
        self.assertTrue(event["in_game_image"])

    def test_optional_offline_modules_do_not_publish_handles_or_reference_counts(self):
        result = self.invoke(optional_modules_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        self.assertNotIn('"event":"native_system_module"', result.stdout)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        absent = [item for item in events if item['event'] == 'native_api_call' and item['name'] == 'sceSysmoduleLoadModule_unavailable']
        self.assertEqual(len(absent), 18)
        self.assertEqual({item['value'] for item in absent}, {7, 0x81, 0x85, 0x8d, 0xb7, 0xe7, 0xb2, 0x1e, 0x21})

    def test_dialog_wrong_namespace_is_not_resolved(self):
        result = self.invoke(synthetic_probe(identifier=nid("sceMsgDialogInitialize") + "#A#A",
                                             name="sceMsgDialogInitialize", library="fixture_library",
                                             module="libSceMsgDialog"), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])["event"], "unimplemented_import")

    def test_dialog_native_state_and_parameter_contracts(self):
        result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"), "--dialog-self-test"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        cases = [e for e in events if e["event"] == "dialog_self_test_case"]
        self.assertEqual(len(cases), 18)
        self.assertTrue(all(e["passed"] for e in cases))
        self.assertEqual(events[-1], {"event": "dialog_self_test", "passed": True})

    def test_time_sysv_clock_and_sleep(self):
        result = self.invoke(time_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_offline_network_failed_init_and_repeated_empty_cleanup(self):
        result = self.invoke(offline_network_cleanup_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        for name, expected in (('sceNetInit_not_supported', 4), ('sceHttpTerm_before_init', 3),
                               ('sceSslTerm_not_supported', 3), ('sceNetPoolDestroy_bad_id', 3), ('sceNetTerm_empty', 4)):
            self.assertEqual(sum(item.get('name') == name for item in events), expected)

    def test_offline_network_cleanup_bindings_require_exact_namespace(self):
        for name, namespace in (('sceHttpTerm', 'libSceHttp'), ('sceSslTerm', 'libSceSsl'),
                                ('sceNetPoolDestroy', 'libSceNet'), ('sceNetTerm', 'libSceNet')):
            for library, module in ((namespace, 'libkernel'), ('libkernel', namespace)):
                result = self.invoke(synthetic_probe(2, nid(name) + '#A#A', name, library, module), '--binding-census')
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                events = [json.loads(line) for line in result.stdout.splitlines()]
                self.assertEqual(next(item['status'] for item in events if item['event'] == 'native_binding_census_import'), 'unbound')

    def test_system_language_circle_confirm_empty_events_and_actual_power_tick(self):
        result = self.invoke(system_parameters_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(sum(item.get('name') == 'sceSystemServiceParamGetInt' for item in events), 2)
        self.assertTrue(any(item.get('name') == 'sceSystemServicePowerTick' for item in events))

    def test_system_parameter_event_power_imports_require_exact_namespace(self):
        for name in ('sceSystemServiceParamGetInt', 'sceSystemServiceReceiveEvent', 'sceSystemServicePowerTick'):
            for library, module in (('libSceSystemService', 'libkernel'), ('libkernel', 'libSceSystemService')):
                result = self.invoke(synthetic_probe(2, nid(name) + '#A#A', name, library, module), '--binding-census')
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                events = [json.loads(line) for line in result.stdout.splitlines()]
                self.assertEqual(next(item['status'] for item in events if item['event'] == 'native_binding_census_import'), 'unbound')

    def test_libc_calendar_host_lifetime_normalization_and_thread_local_storage(self):
        result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--libc-calendar-self-test'],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        cases = [item for item in events if item['event'] == 'libc_calendar_self_test_case']
        self.assertEqual(len(cases), 12)
        self.assertTrue(all(item['passed'] for item in cases))
        self.assertEqual(events[-1], dict(event='libc_calendar_self_test', cases=12, passed=True))

    def test_libc_gmtime_sysv_signed_time64_gregorian_fields_and_unchanged_errno(self):
        for seconds in (0, -1, 951782400, 2147483648, 2208988800, -62135596800, 253402300799):
            date = datetime.datetime(1970, 1, 1) + datetime.timedelta(seconds=seconds)
            expected = (date.second, date.minute, date.hour, date.day, date.month - 1, date.year - 1900,
                        (date.weekday() + 1) % 7, (date - datetime.datetime(date.year, 1, 1)).days, 0)
            result = self.invoke(libc_calendar_fixture('gmtime', seconds, expected_tm=expected), '--enter')
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        result = self.invoke(libc_calendar_fixture('gmtime', 0x7fffffffffffffff, error=84), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_libc_localtime_mktime_time64_normalization_and_exact_36byte_output(self):
        for seconds in (0, 2147483648, 2208988800):
            value = host_time.localtime(seconds)
            expected = (value.tm_sec, value.tm_min, value.tm_hour, value.tm_mday, value.tm_mon - 1,
                        value.tm_year - 1900, (value.tm_wday + 1) % 7, value.tm_yday - 1, value.tm_isdst)
            result = self.invoke(libc_calendar_fixture('localtime', seconds, expected_tm=expected), '--enter')
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            dirty_derived = expected[:6] + (73, 73, expected[8])
            result = self.invoke(libc_calendar_fixture('mktime', input_tm=dirty_derived, expected_tm=expected,
                                                       expected_seconds=seconds), '--enter')
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        normalized_seconds = int(datetime.datetime(2026, 1, 1, 0, 0, 30).timestamp())
        value = host_time.localtime(normalized_seconds)
        normalized = (30, 0, 0, 1, 0, 126, (value.tm_wday + 1) % 7, value.tm_yday - 1, value.tm_isdst)
        result = self.invoke(libc_calendar_fixture('mktime', input_tm=(90,59,23,0,0,126,73,73,-1),
                                                   expected_tm=normalized, expected_seconds=normalized_seconds), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        result = self.invoke(libc_calendar_fixture('mktime', input_tm=(0,0,0,1,0,0x7fffffff,73,73,-1), error=84), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_libc_time_utc_and_calendar_pointer_errors_do_not_touch_output(self):
        now = int(host_time.time())
        for null_output in (False, True):
            result = self.invoke(libc_calendar_fixture('time', bounds=(now - 1, now + 10), null_output=null_output), '--enter')
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        for api in ('time', 'gmtime', 'localtime', 'mktime'):
            pointers = (1, 'readonly') if api in ('time', 'mktime') else (0, 1)
            for pointer in pointers:
                result = self.invoke(libc_calendar_fixture(api, input_tm=(0,0,0,1,0,126,73,73,-1) if api == 'mktime' else None,
                                                           error=14, bad_pointer=pointer), '--enter')
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_libc_calendar_imports_require_exact_library_and_module(self):
        for name in ('time', 'gmtime', 'localtime', 'mktime'):
            for library, module in (('libc', 'libkernel'), ('libkernel', 'libc')):
                result = self.invoke(synthetic_probe(2, nid(name) + '#A#A', name, library, module), '--binding-census')
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                events = [json.loads(line) for line in result.stdout.splitlines()]
                self.assertEqual(next(item['status'] for item in events if item['event'] == 'native_binding_census_import'), 'unbound')

    def test_rtc_local_timezone_dst_transitions_and_fractional_offsets(self):
        result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--rtc-self-test'],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        cases = [item for item in events if item['event'] == 'rtc_self_test_case']
        self.assertEqual(len(cases), 18)
        self.assertTrue(all(item['passed'] for item in cases))
        self.assertEqual(events[-1], dict(event='rtc_self_test', cases=18, passed=True))

    def test_rtc_network_named_query_has_explicit_offline_host_utc_policy(self):
        delta = datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None) - datetime.datetime(1, 1, 1)
        now = (delta.days * 86400 + delta.seconds) * 1000000 + delta.microseconds
        result = self.invoke(rtc_fixture(current_bounds=(now - 1000000, now + 10000000),
                                        clock_api='sceRtcGetCurrentNetworkTick'), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        source = next(item for item in events if item['event'] == 'native_rtc_clock_source')
        self.assertEqual(source['provider'], 'windows_utc')
        self.assertEqual(source['policy'], 'offline_wall_clock_compatibility')
        self.assertFalse(source['network_time_verified'])
        self.assertFalse(source['psn_authenticated'])

    def test_rtc_set_filetime_64bit_epoch_maximum_and_100ns_truncation(self):
        epoch = datetime.datetime(1601, 1, 1)
        for date in ((1601, 1, 1, 0, 0, 0, 0), (1970, 1, 1, 0, 0, 0, 0),
                     (2026, 10, 17, 3, 4, 5, 123456), (9999, 12, 31, 23, 59, 59, 999999)):
            delta = datetime.datetime(*date) - epoch
            value = (delta.days * 86400 + delta.seconds) * 10000000 + delta.microseconds * 10
            for remainder in (0, 9):
                with self.subTest(date=date, remainder=remainder):
                    result = self.invoke(rtc_conversion_fixture('sceRtcSetWin32FileTime', value + remainder, date), '--enter')
                    self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        for value in (2650467744000000000, 0xffffffffffffffff):
            result = self.invoke(rtc_conversion_fixture('sceRtcSetWin32FileTime', value, error=0x80b50003), '--enter')
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_rtc_local_conversion_sysv_matches_host_timezone_preserves_microseconds_and_alias(self):
        epoch = datetime.datetime(1, 1, 1)
        for date in ((2025, 12, 31, 20, 0, 0, 123456), (2026, 7, 1, 0, 0, 0, 999999)):
            utc = datetime.datetime(*date, tzinfo=datetime.timezone.utc)
            local = utc.astimezone().replace(tzinfo=None)
            delta = utc.replace(tzinfo=None) - epoch
            value = (delta.days * 86400 + delta.seconds) * 1000000 + delta.microseconds
            delta = local - epoch
            expected = (delta.days * 86400 + delta.seconds) * 1000000 + delta.microseconds
            for alias in (False, True):
                result = self.invoke(rtc_conversion_fixture('sceRtcConvertUtcToLocalTime', value, expected, alias=alias), '--enter')
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_rtc_clock_and_conversion_reject_bad_pointers_and_keep_output_tails(self):
        for api in ('sceRtcGetCurrentNetworkTick', 'sceRtcSetWin32FileTime', 'sceRtcConvertUtcToLocalTime'):
            modes = ['output_null', 'output_invalid', 'output_readonly']
            if api == 'sceRtcConvertUtcToLocalTime':
                modes += ['input_null', 'input_invalid']
            for mode in modes:
                result = self.invoke(rtc_conversion_fixture(api, error=0x80b50002, bad_pointer=mode), '--enter')
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        result = self.invoke(rtc_conversion_fixture('sceRtcConvertUtcToLocalTime', 0xffffffffffffffff,
                                                    error=0x80b50003, alias=True), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_rtc_gregorian_utc_roundtrip_filetime_epochs_and_output_bounds(self):
        epoch = datetime.datetime(1, 1, 1)
        file_epoch = datetime.datetime(1601, 1, 1)
        for date in ((1, 1, 1, 0, 0, 0, 0), (1600, 2, 29, 0, 0, 0, 1),
                     (1601, 1, 1, 0, 0, 0, 0), (1900, 3, 1, 0, 0, 0, 0),
                     (1970, 1, 1, 0, 0, 0, 0), (2000, 2, 29, 23, 59, 59, 999999),
                     (2100, 3, 1, 0, 0, 0, 0), (2026, 10, 17, 3, 4, 5, 123456),
                     (9999, 12, 31, 23, 59, 59, 999999)):
            with self.subTest(date=date):
                value = datetime.datetime(*date)
                delta = value - epoch
                tick = (delta.days * 86400 + delta.seconds) * 1000000 + delta.microseconds
                delta = value - file_epoch
                filetime = max(0, (delta.days * 86400 + delta.seconds) * 10000000 + delta.microseconds * 10)
                result = self.invoke(rtc_fixture(date, tick=tick, filetime=filetime), '--enter')
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_rtc_invalid_calendar_tick_and_pointers_leave_outputs_unchanged(self):
        for date, error in (((0, 1, 1, 0, 0, 0, 0), 8), ((10000, 1, 1, 0, 0, 0, 0), 8),
                            ((2000, 13, 1, 0, 0, 0, 0), 9), ((2000, 0, 1, 0, 0, 0, 0), 9),
                            ((1900, 2, 29, 0, 0, 0, 0), 10), ((2000, 2, 30, 0, 0, 0, 0), 10),
                            ((2000, 1, 0, 0, 0, 0, 0), 10), ((2000, 1, 1, 24, 0, 0, 0), 11),
                            ((2000, 1, 1, 0, 60, 0, 0), 12), ((2000, 1, 1, 0, 0, 60, 0), 13),
                            ((2000, 1, 1, 0, 0, 0, 1000000), 14)):
            with self.subTest(date=date):
                result = self.invoke(rtc_fixture(date, error=0x80b50000 + error), '--enter')
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        for tick in (315537897600000000, 0xffffffffffffffff):
            result = self.invoke(rtc_fixture(tick=tick, error=0x80b50003), '--enter')
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        result = self.invoke(rtc_fixture(bad_pointer=True), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_rtc_current_time_matches_host_utc_and_roundtrips_with_alias(self):
        delta = datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None) - datetime.datetime(1, 1, 1)
        now = (delta.days * 86400 + delta.seconds) * 1000000 + delta.microseconds
        result = self.invoke(rtc_fixture(current_bounds=(now - 1000000, now + 10000000)), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertTrue(any(item.get('name') == 'sceRtcGetCurrentTick' for item in events))

    def test_rtc_bindings_require_exact_library_and_module(self):
        for name in ('sceRtcGetCurrentTick', 'sceRtcSetTick', 'sceRtcGetTick', 'sceRtcGetWin32FileTime',
                     'sceRtcGetCurrentNetworkTick', 'sceRtcSetWin32FileTime', 'sceRtcConvertUtcToLocalTime'):
            for library, module in (('libSceRtc', 'libkernel'), ('libkernel', 'libSceRtc')):
                result = self.invoke(synthetic_probe(2, nid(name) + '#A#A', name, library, module), '--binding-census')
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                events = [json.loads(line) for line in result.stdout.splitlines()]
                self.assertEqual(next(item['status'] for item in events if item['event'] == 'native_binding_census_import'), 'unbound')

    def test_counter_sysv_tsc_domain_frequency_family_and_exact_kernel_namespace(self):
        result = self.invoke(counter_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        clock = next(item for item in events if item['event'] == 'native_counter_clock')
        self.assertEqual(clock['domain'], 'host_invariant_tsc')
        self.assertTrue(clock['frequency_is_estimate'])
        self.assertLessEqual(clock['sample_spread_ppm'], 10000)
        for name in ('sceKernelGetProcessTimeCounter', 'sceKernelGetProcessTimeCounterFrequency',
                     'sceKernelReadTsc', 'sceKernelGetTscFrequency'):
            for library, module in (('libkernel', 'libc'), ('libc', 'libkernel')):
                image = synthetic_probe(2, nid(name) + '#A#A', name, library, module)
                result = self.invoke(image, '--binding-census')
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                records = [json.loads(line) for line in result.stdout.splitlines()]
                self.assertEqual(next(item['status'] for item in records if item['event'] == 'native_binding_census_import'), 'unbound')

    def test_memory_native_ranges_aliases_permissions_and_lifecycle(self):
        result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"), "--memory-self-test"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        cases = [event for event in events if event["event"] == "memory_self_test_case"]
        self.assertEqual(len(cases), 44)
        self.assertTrue(all(event["passed"] for event in cases))
        capacity = next(event for event in events if event['event'] == 'direct_memory_capacity_failure' and event['allocated_bytes'] > (1 << 32))
        self.assertEqual(capacity['capacity_bytes'], 9 << 29)
        self.assertEqual(capacity['allocated_bytes'], 0x10a800000)
        self.assertEqual(capacity['largest_aligned_free_bytes'], 0)
        self.assertEqual(events[-1], {"event": "memory_self_test", "passed": True})

    def test_mapped_write_tracker_aliases_generation_and_controlled_concurrent_interleavings(self):
        result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--write-tracker-self-test'],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        cases = [event for event in events if event['event'] == 'write_tracker_self_test_case']
        self.assertEqual(len(cases), 44)
        self.assertTrue(all(event['passed'] for event in cases))
        self.assertEqual(events[-1], {'event': 'write_tracker_self_test', 'passed': True})

    def test_tracked_texture_cache_requires_valid_generation_layout_live_memory_and_owned_pixels(self):
        result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--tracked-texture-self-test'],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        cases = [event for event in events if event['event'] == 'tracked_texture_self_test_case']
        self.assertEqual(len(cases), 18)
        self.assertTrue(all(event['passed'] for event in cases))
        self.assertEqual(events[-1], {'event': 'tracked_texture_self_test', 'passed': True})

    def test_texture_validation_modes_are_explicit_and_reject_invalid_or_duplicate_options(self):
        for mode in ('full', 'tracked'):
            result = self.invoke(exit_fixture(), '--enter', texture_validation=mode)
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            config = next(json.loads(line) for line in result.stdout.splitlines()
                          if json.loads(line)['event'] == 'native_texture_validation_configuration')
            self.assertEqual(config['mode'], mode)
        for mode in ('', 'Full', 'off', 'hash'):
            result = self.invoke(exit_fixture(), '--enter', texture_validation=mode)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertNotIn('image_prepared', result.stdout)
        for action in ('--validate', '--trace-enter', '--binding-census'):
            result = self.invoke(exit_fixture(), action, texture_validation='full')
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        duplicate = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), 'unused.ktprobe', '--enter',
                                   '--texture-validation', 'full', '--texture-validation', 'tracked'],
                                  capture_output=True, text=True, timeout=5)
        self.assertEqual(duplicate.returncode, 2, duplicate.stdout + duplicate.stderr)

    def test_attachment_validation_modes_reject_invalid_options_and_keep_texture_mode_independent(self):
        for mode in ('upload', 'reuse'):
            result = self.invoke(exit_fixture(), '--enter', attachment_validation=mode, texture_validation='tracked')
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertEqual(next(e['mode'] for e in events if e['event'] == 'native_attachment_validation_configuration'), mode)
            self.assertEqual(next(e['mode'] for e in events if e['event'] == 'native_texture_validation_configuration'), 'tracked')
        for mode in ('', 'Reuse', 'resident', 'none'):
            result = self.invoke(exit_fixture(), '--enter', attachment_validation=mode)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        for action in ('--validate', '--trace-enter', '--binding-census'):
            result = self.invoke(exit_fixture(), action, attachment_validation='reuse')
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        duplicate = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), 'unused.ktprobe', '--enter',
                                   '--attachment-validation', 'reuse', '--attachment-validation', 'upload'],
                                  capture_output=True, text=True, timeout=5)
        self.assertEqual(duplicate.returncode, 2, duplicate.stdout + duplicate.stderr)

    def test_memory_sysv_shared_subrange_and_seventh_named_argument(self):
        result = self.invoke(direct_memory_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        maps = [event for event in events if event["event"] == "direct_memory_map"]
        self.assertEqual([event["size"] for event in maps], [0x10000, 0x4000])
        self.assertEqual(int(maps[1]["physical"], 16), 0x4000)

    def test_memory_read_only_mapping_rejects_writes(self):
        result = self.invoke(direct_memory_fixture("readonly"), "--enter")
        self.assertEqual(result.returncode, 22, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])["access"], "0x1")

    def test_memory_gpu_only_mapping_rejects_cpu_reads(self):
        result = self.invoke(direct_memory_fixture("noaccess"), "--enter")
        self.assertEqual(result.returncode, 22, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])["access"], "0x0")

    def test_memory_release_invalidates_all_aliases(self):
        result = self.invoke(direct_memory_fixture("release_access"), "--enter")
        self.assertEqual(result.returncode, 22, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])["access"], "0x0")

    def test_memory_partial_operations_stop_instead_of_succeeding(self):
        for action, name in (("partial_release", "sceKernelReleaseDirectMemory"),
                             ("partial_unmap", "sceKernelMunmap")):
            with self.subTest(action=action):
                result = self.invoke(direct_memory_fixture(action), "--enter")
                self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
                event = json.loads(result.stdout.splitlines()[-1])
                self.assertEqual(event["event"], "unimplemented_api")
                self.assertEqual(event["name"], name)
                self.assertTrue(event["in_game_image"])

    def test_memory_wrong_namespace_is_not_resolved(self):
        result = self.invoke(direct_memory_fixture(library="not_libkernel"), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertIn('"event":"unimplemented_import"', result.stdout)

    def test_mspace_native_partition_alignment_and_coalescing(self):
        result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"), "--mspace-self-test"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        cases = [event for event in events if event["event"] == "mspace_self_test_case"]
        self.assertEqual(len(cases), 9)
        self.assertTrue(all(event["passed"] for event in cases))

    def test_mspace_sysv_backing_overlap_errno_and_lifecycle(self):
        result = self.invoke(mspace_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_mspace_live_arena_pins_backing(self):
        for action, name in (("release_pinned", "sceKernelReleaseDirectMemory"),
                             ("unmap_pinned", "sceKernelMunmap"),
                             ("protect_pinned", "sceKernelMprotect")):
            with self.subTest(action=action):
                result = self.invoke(mspace_fixture(action), "--enter")
                self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
                event = json.loads(result.stdout.splitlines()[-1])
                self.assertEqual(event["name"], name)
                self.assertTrue(event["in_game_image"])

    def test_mspace_double_free_stops(self):
        result = self.invoke(mspace_fixture("double_free"), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])["name"], "invalid_mspace_free")

    def test_mspace_destroyed_handle_is_not_reused(self):
        result = self.invoke(mspace_fixture("stale_handle"), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])["name"], "invalid_mspace_handle")

    def test_video_native_window_lifecycle_and_resolution(self):
        result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"), "--video-self-test"],
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        cases = [event for event in events if event["event"] == "video_self_test_case"]
        self.assertEqual(len(cases), 17)
        markers = [event for event in events if event["event"] == "native_timeline_marker"]
        self.assertEqual([event['id'] for event in markers], [1, 2])
        self.assertTrue(all(event['source'] == 'F8' and event['clock_us'] > 0 for event in markers))
        self.assertLessEqual(markers[0]['clock_us'], markers[1]['clock_us'])
        self.assertTrue(all(event["passed"] for event in cases))
        self.assertEqual([e['reason'] for e in events if e['event'] == 'native_video_execute_unavailable'],
                         ['window_close_requested', 'invalid_port_or_buffer', 'invalid_port_or_buffer', 'window_close_requested', 'window_shutdown_in_progress'])
        self.assertEqual([e['reason'] for e in events if e['event'] == 'native_video_present_unavailable'],
                         ['window_close_requested', 'invalid_port_or_buffer', 'invalid_port_or_buffer', 'window_shutdown_in_progress'])
        self.assertEqual(sum(e['event'] == 'native_video_window_close_requested' for e in events), 2)
        self.assertEqual(sum(e['event'] == 'native_presentation_shutdown_begin' for e in events), 1)
        retired = [e for e in events if e['event'] == 'native_presentation_shutdown_completed']
        self.assertEqual(len(retired), 1)
        self.assertTrue(all(retired[0][field] for field in ('display_clock_stopped', 'render_fences_drained',
            'presentation_detached', 'window_destroyed', 'window_thread_joined', 'window_handles_closed', 'label_metadata_retained')))
        self.assertFalse(retired[0]['resident_gpu_context_destroyed'])
        self.assertFalse(retired[0]['legacy_device_destroyed'])
        cancelled = next(e for e in events if e['event'] == 'gnm_close_self_test')
        self.assertTrue(all(cancelled[field] for field in ('cancelled', 'queue_lock_released',
            'completed_frame_unchanged', 'command_memory_released', 'passed')))
        self.assertFalse(any(e['event'] == 'native_gnm_frame_completed' for e in events))

    def test_display_layout_native_padding_and_formats(self):
        result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"), "--display-layout-self-test"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        cases = [event for event in events if event["event"] == "display_layout_self_test_case"]
        self.assertEqual(len(cases), 10)
        self.assertTrue(all(event["passed"] for event in cases))

    @requires_vulkan_device
    def test_graphics_real_device_images_aliases_rollback_and_teardown(self):
        result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"), "--graphics-self-test"],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        cases = [event for event in events if event["event"] == "graphics_self_test_case"]
        self.assertEqual(len(cases), 10)
        self.assertTrue(all(event["passed"] for event in cases))
        self.assertTrue(any(event["event"] == "native_graphics_device" and event["backend"] == "Vulkan" for event in events))
        raster = [event for event in events if event['event'] == 'raster_self_test_case']
        self.assertEqual(len(raster), 36)
        self.assertTrue(all(event['passed'] for event in raster))
        attachments = [event for event in events if event['event'] == 'attachment_self_test_case']
        self.assertEqual(len(attachments), 18)
        self.assertTrue(all(event['passed'] for event in attachments))
        adapters = [event for event in events if event['event'] == 'gpu_adapter_self_test_case']
        self.assertEqual(len(adapters), 9)
        self.assertTrue(all(event['passed'] for event in adapters))
        tracked = [event for event in events if event['event'] == 'tracked_texture_gpu_self_test_case']
        self.assertEqual(len(tracked), 6)
        self.assertTrue(all(event['passed'] for event in tracked))
        self.assertTrue(any(event['event'] == 'native_graphics_present_completed' for event in events))
        window = next(event for event in events if event['event'] == 'native_video_open')
        swapchain = next(event for event in events if event['event'] == 'native_graphics_swapchain')
        self.assertGreater(window['dpi'], 0)
        self.assertEqual((swapchain['width'], swapchain['height']), (window['width'], window['height']))
        self.assertTrue(any(e['event'] == 'native_video_present_wait' and e['reason'] == 'zero_surface_extent' for e in events))
        retired = next(e for e in events if e['event'] == 'native_presentation_shutdown_completed')
        self.assertTrue(retired['legacy_device_destroyed'])
        self.assertTrue(retired['render_fences_drained'])
        self.assertTrue(retired['window_thread_joined'])
        self.assertFalse(retired['resident_gpu_context_destroyed'])

    @requires_vulkan_device
    def test_graphics_registration_sysv_lifecycle(self):
        result = self.invoke(graphics_registration_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    @requires_vulkan_device
    def test_graphics_pins_preserve_gpu_permissions(self):
        result = self.invoke(graphics_registration_fixture(protect_pinned=True), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])["name"], "sceKernelMprotect")

    def test_video_sysv_open_query_and_close(self):
        result = self.invoke(video_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_video_unknown_parameter_stops(self):
        result = self.invoke(video_fixture("parameter"), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        event = json.loads(result.stdout.splitlines()[-1])
        self.assertEqual(event["name"], "sceVideoOutOpen_parameter")
        self.assertTrue(event["in_game_image"])

    def test_video_wrong_namespace_is_not_resolved(self):
        result = self.invoke(video_fixture(library="not_video_out"), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])["event"], "unimplemented_import")

    def test_video_attribute_sysv_fields_reserved_and_seventh_argument(self):
        result = self.invoke(video_attribute_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_video_attribute_null_diagnostic(self):
        result = self.invoke(video_attribute_fixture(null=True), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])["name"], "sceVideoOutSetBufferAttribute_null")

    def test_libc_memory_sysv_copy_overlap_fill_unsigned_compare_and_length(self):
        result = self.invoke(libc_memory_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_libc_memory_zero_length_does_not_touch_storage(self):
        result = self.invoke(libc_memory_fixture(zero=True), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_libc_memory_wrong_namespace_is_not_resolved(self):
        result = self.invoke(libc_memory_fixture(library="not_libc"), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)

    def test_profile_sysv_base_mode(self):
        result = self.invoke(profile_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        query = next(event for event in events if event.get("name") == "sceKernelIsNeoMode")
        self.assertEqual(query["value"], 0)
        self.assertTrue(query["in_game_image"])

    def test_profile_wrong_namespace_is_not_resolved(self):
        result = self.invoke(profile_fixture(library="not_libkernel"), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)

    def test_cxx_guards_native_contention_abort_and_visibility(self):
        result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"), "--cxx-guard-self-test"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        cases = [event for line in result.stdout.splitlines()
                 if (event := json.loads(line))["event"] == "cxx_guard_self_test_case"]
        self.assertEqual(len(cases), 5)
        self.assertTrue(all(event["passed"] for event in cases))

    def test_cxx_guards_sysv_acquire_abort_retry_release(self):
        result = self.invoke(cxx_guard_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_cxx_guard_recursion_stops_without_deadlock(self):
        result = self.invoke(cxx_guard_fixture("recursive"), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])["name"], "recursive_cxa_guard_initialization")

    def test_cxx_guard_invalid_address_and_ownership_stop(self):
        for action, name in (("not_owned", "cxa_guard_not_owned"), ("misaligned", "invalid_cxa_guard_address")):
            with self.subTest(action=action):
                result = self.invoke(cxx_guard_fixture(action), "--enter")
                self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
                self.assertEqual(json.loads(result.stdout.splitlines()[-1])["name"], name)

    def test_cxx_guard_wrong_namespace_is_not_resolved(self):
        result = self.invoke(cxx_guard_fixture(library="not_libc"), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)

    def test_time_native_monotonicity_and_microseconds(self):
        result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"), "--time-self-test"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        event = next(item for item in events if item['event'] == 'time_self_test')
        self.assertTrue(event["passed"])
        self.assertGreaterEqual(event["elapsed_us"], event["requested_us"])
        self.assertTrue(event['counter_valid'])
        self.assertGreaterEqual(event['qpc_us'], 30000)

    def test_equeue_native_packing_timeout_and_delete_wakeup(self):
        result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"), "--equeue-self-test"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        cases = [event for line in result.stdout.splitlines() if (event := json.loads(line))["event"] == "equeue_self_test_case"]
        self.assertEqual(len(cases), 6)
        self.assertTrue(all(event["passed"] for event in cases))

    def test_pad_native_mapping_focus_history_and_real_backend(self):
        result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"), "--pad-self-test"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        cases = [event for line in result.stdout.splitlines() if (event := json.loads(line))["event"] == "pad_self_test_case"]
        self.assertEqual(len(cases), 33)
        self.assertTrue(all(event["passed"] for event in cases))

    def test_mouse_pad_binding_explicit_entry_only_and_default_off(self):
        default = self.invoke(exit_fixture(), '--enter')
        self.assertEqual(default.returncode, 7, default.stdout + default.stderr)
        self.assertNotIn('native_pad_input_binding', default.stdout)
        for name, mask in (('none', 0), ('cross', 0x4000), ('circle', 0x2000), ('square', 0x8000),
                           ('triangle', 0x1000), ('options', 8), ('touchpad', 0x100000)):
            with self.subTest(name=name):
                result = self.invoke(exit_fixture(), '--enter', mouse_button=name)
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
                binding = next(json.loads(line) for line in result.stdout.splitlines()
                               if json.loads(line)['event'] == 'native_pad_input_binding')
                self.assertEqual(binding['binding'], mask)
                self.assertTrue(binding['physical_edges_only'])

    def test_mouse_pad_binding_rejects_invalid_names_modes_and_duplicates(self):
        for name in ('', 'Triangle', 'coin', '1000'):
            result = self.invoke(exit_fixture(), '--enter', mouse_button=name)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertNotIn('enter_game', result.stdout)
        for mode in ('--validate', '--binding-census', '--trace-enter'):
            result = self.invoke(exit_fixture(), mode, mouse_button='triangle')
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertNotIn('image_prepared', result.stdout)
        executable = str(ROOT / 'out/native/KetsuiNativeProbe.exe')
        duplicate = subprocess.run([executable, 'unused.ktprobe', '--enter', '--mouse-left-pad-button',
                                    'triangle', '--mouse-left-pad-button', 'cross'], capture_output=True,
                                   text=True, timeout=5)
        self.assertEqual(duplicate.returncode, 2, duplicate.stdout + duplicate.stderr)
        demo = subprocess.run([executable, '--pad-demo', '--mouse-left-pad-button', 'not-a-button'],
                              capture_output=True, text=True, timeout=5)
        self.assertEqual(demo.returncode, 2, demo.stdout + demo.stderr)

    def test_ngs2_waveform_frontier_is_bounded_and_never_reports_a_parse(self):
        for valid in (True,):
            with self.subTest(valid=valid):
                asm = SyntheticCode()
                asm.labels['parse'] = 4112
                asm.labels['output'] = 4400
                asm.emit('48 83 ec 08')
                if valid:
                    asm.relative('48 8d 3d', 'waveform')
                else:
                    asm.emit('bf 01 00 00 00')
                asm.emit('be 2c 01 00 00')
                asm.relative('48 8d 15', 'output')
                asm.relative('ff 15', 'parse')
                asm.emit('0f 0b')
                asm.label('waveform')
                payload = bytes(range(256)) + bytes(44)
                asm.code.extend(payload)
                result = self.invoke(interfaces_fixture(asm.finish(), [('sceNgs2ParseWaveformData', 'libSceNgs2')], data_size=512), '--enter')
                self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
                events = [json.loads(line) for line in result.stdout.splitlines()]
                frontier = next(e for e in events if e['event'] == 'native_ngs2_waveform_frontier')
                self.assertEqual(frontier['input_bytes'], 300)
                self.assertEqual(frontier['captured_bytes'], 256 if valid else 0)
                self.assertEqual(frontier['prefix_hex'], payload[:256].hex() if valid else '')
                self.assertFalse(frontier['guest_output_written'])
                self.assertEqual(events[-1]['name'], 'sceNgs2ParseWaveformData_analysis_pending')

    def test_ngs2_vag_sysv_info_layout_errors_preserve_output_and_tail(self):
        for action in ('valid', 'short', 'zero_size', 'bad_pointer', 'null_output', 'bad_rate', 'bad_predictor'):
            with self.subTest(action=action):
                result = self.invoke(vag_parse_fixture(action), '--enter')
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
                self.assertEqual('native_ngs2_waveform_parsed' in result.stdout, action == 'valid')

    def test_ngs2_vag_loop_and_wrong_namespaces_do_not_claim_support(self):
        result = self.invoke(vag_parse_fixture('loop'), '--enter')
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertIn('VAG embedded loop', result.stdout)
        self.assertNotIn('native_ngs2_waveform_parsed', result.stdout)
        for library, module in (('fixture', 'libSceNgs2'), ('libSceNgs2', 'fixture')):
            result = self.invoke(vag_parse_fixture(library=library, module=module), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertIn('unimplemented_import', result.stdout)

    def test_np_native_offline_callback_lifecycle_and_reentrancy(self):
        result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"), "--np-self-test"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        cases = [event for line in result.stdout.splitlines() if (event := json.loads(line))["event"] == "np_self_test_case"]
        self.assertEqual(len(cases), 27)
        self.assertTrue(all(event["passed"] for event in cases))

    def test_np_request_native_bounded_lifecycle_offline_outputs_and_concurrency(self):
        for _ in range(3):
            result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--np-request-self-test'],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            cases = [event for event in events if event['event'] == 'np_request_self_test_case']
            self.assertEqual(len(cases), 10)
            self.assertTrue(all(event['passed'] for event in cases))
            self.assertEqual(events[-1], {'event': 'np_request_self_test', 'passed': True})

    def test_np_extended_sysv_metadata_async_submission_poll_abort_and_stale_handles(self):
        result = self.invoke(np_extended_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        self.assertNotIn('FIXTURE_0001', result.stdout)
        self.assertNotIn('5a' * 128, result.stdout.lower())
        events = [json.loads(line) for line in result.stdout.splitlines()]
        completions = [item for item in events if item['event'] == 'native_np_request_complete']
        self.assertEqual(len(completions), 1)
        self.assertEqual(completions[0]['result'], -2141913082)
        self.assertFalse(completions[0]['online'])

    def test_np_extended_imports_require_exact_library_and_module(self):
        for name in ('sceNpSetContentRestriction', 'sceNpSetNpTitleId', 'sceNpCreateAsyncRequest',
                     'sceNpPollAsync', 'sceNpAbortRequest'):
            for library, module, expected in (('libSceNpManager', 'libSceNpManager', 'host_bound'),
                    ('libSceNpCommon', 'libSceNpManager', 'unbound'), ('libSceNpManager', 'libkernel', 'unbound')):
                image = synthetic_probe(2, nid(name) + '#A#A', name, library, module)
                result = self.invoke(image, '--binding-census')
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                records = [json.loads(line) for line in result.stdout.splitlines()]
                self.assertEqual(next(item['status'] for item in records if item['event'] == 'native_binding_census_import'), expected)

    def test_netctl_sysv_disconnected_output_invalid_addresses_and_exact_namespace(self):
        for mode in ('normal', 'null', 'invalid', 'readonly'):
            result = self.invoke(net_state_fixture(mode=mode), '--enter')
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        for library, module in (('libSceNet', 'libSceNetCtl'), ('libSceNetCtl', 'libSceNet')):
            result = self.invoke(net_state_fixture(library=library, module=module), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'sceNetCtlGetState')

    def test_np_request_sysv_synchronous_lifecycle_outputs_and_offline_status(self):
        result = self.invoke(np_request_fixture(), '--enter')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        completed = [event for event in events if event['event'] == 'native_np_request_complete']
        self.assertEqual(len(completed), 3)
        self.assertTrue(all(event['result'] == -2141913082 and not event['online'] for event in completed))

    def test_np_request_requires_exact_manager_library_and_module(self):
        for library, module in (('libSceNpCommon', 'libSceNpManager'), ('libSceNpManager', 'libkernel')):
            result = self.invoke(np_request_fixture(library=library, module=module), '--enter')
            self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
            self.assertEqual(json.loads(result.stdout.splitlines()[-1])['name'], 'sceNpCreateRequest')

    def test_display_clock_native_period_waits_and_cancellation(self):
        result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"), "--display-clock-self-test"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        cases = [event for line in result.stdout.splitlines() if (event := json.loads(line))["event"] == "display_clock_self_test_case"]
        self.assertEqual(len(cases), 8)
        self.assertTrue(all(event["passed"] for event in cases))

    def test_video_clock_queue_sysv_wait_and_event_fields(self):
        result = self.invoke(video_clock_queue_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_video_close_removes_pending_event_subscriptions(self):
        result = self.invoke(video_clock_queue_fixture(close_first=True), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_video_flip_event_payload_is_sign_extended(self):
        result = self.invoke(video_flip_payload_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_user_sysv_identity_event_and_utf8_name(self):
        result = self.invoke(user_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_pad_sysv_layout_read_and_close(self):
        result = self.invoke(pad_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_np_sysv_registration_does_not_invent_sign_out_and_cancellation(self):
        result = self.invoke(np_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_np_wrong_namespace_is_not_resolved(self):
        result = self.invoke(np_fixture("notNp"), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertIn('"name":"sceNpRegisterStateCallbackA"', result.stdout)

    def test_pad_wrong_namespace_is_not_resolved(self):
        result = self.invoke(pad_fixture("notPad"), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        self.assertIn('"name":"scePadInit"', result.stdout)

    def test_user_native_lifecycle_and_buffers(self):
        result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"), "--user-self-test"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        cases = [e for e in events if e["event"] == "user_self_test_case"]
        self.assertEqual(len(cases), 7)
        self.assertTrue(all(e["passed"] for e in cases))
        self.assertEqual(events[-1], {"event": "user_self_test", "passed": True})

    def test_screenshot_native_encoder_pixels_stride_and_preservation(self):
        with tempfile.TemporaryDirectory(prefix="kt-screenshot-") as directory:
            root = Path(directory)
            result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"),
                                     "--screenshot-self-test", str(root)],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            cases = [e for e in events if e["event"] == "screenshot_self_test_case"]
            self.assertEqual(len(cases), 7)
            self.assertTrue(all(e["passed"] for e in cases))
            width, height, pixels = decoded_test_png(root / "pixels.png")
            self.assertEqual((width, height), (2, 2))
            self.assertEqual(pixels, bytes([255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,128]))
            self.assertFalse((root / "disabled.png").exists())
            self.assertEqual([path.name for path in root.iterdir()], ["pixels.png"])

    def test_screenshot_sysv_switch_and_module_lifecycle(self):
        result = self.invoke(screenshot_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_storage_initialization_preserves_existing_files(self):
        with tempfile.TemporaryDirectory(prefix="kt-storage-") as directory:
            root = Path(directory)
            saves = root / "profiles/1000/CUSA09062/savedata"
            saves.mkdir(parents=True)
            sentinel = saves / "existing.bin"
            expected = b"Existing synthetic save data must survive initialization"
            sentinel.write_bytes(expected)
            result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"),
                                     "--storage-self-test", str(root)],
                                    capture_output=True, text=True, timeout=10, cwd=ROOT)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            cases = [e for e in events if e["event"] == "storage_self_test_case"]
            self.assertEqual(len(cases), 16)
            self.assertTrue(all(e["passed"] for e in cases))
            self.assertEqual(sentinel.read_bytes(), expected)
            self.assertEqual([path.name for path in saves.iterdir()], ["existing.bin"])

    def test_storage_rejects_original_data_and_workspace_roots(self):
        for root in (ROOT, ROOT / "game-data/should-not-create", ROOT.anchor):
            with self.subTest(root=str(root)):
                result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"),
                                         "--storage-self-test", str(root)],
                                        capture_output=True, text=True, timeout=10, cwd=ROOT)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn("separate", result.stderr)

    def test_storage_sysv_initialization_and_trace_path_forwarding(self):
        for mode in ("--enter", "--trace-enter"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(prefix="kt-storage-abi-") as directory:
                user_root = Path(directory) / "profile with spaces 存档"
                result = self.invoke(storage_fixture(), mode, user_directory=str(user_root) + "\\")
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
                saves = user_root / "profiles/1000/CUSA09062/savedata"
                self.assertTrue(saves.is_dir())
                self.assertEqual(list(saves.iterdir()), [])

    def test_storage_missing_read_mount_preserves_output_and_does_not_create_save(self):
        with tempfile.TemporaryDirectory(prefix="kt-storage-mount-") as directory:
            result = self.invoke(storage_mount_fixture(), "--enter", user_directory=directory)
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            saves = Path(directory) / "profiles/1000/CUSA09062/savedata"
            self.assertEqual(list(saves.iterdir()), [])
            events = [json.loads(line) for line in result.stdout.splitlines()]
            mount = next(e for e in events if e["event"] == "native_storage_mount")
            self.assertEqual(mount["directory"], "Fixture")
            self.assertEqual(mount["result"], "not_found")
            self.assertFalse(mount["output_modified"])

    def test_storage_corrupt_or_exclusive_mounts_do_not_hide_or_modify_existing_data(self):
        for mode in (1, 6):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(prefix="kt-storage-existing-") as directory:
                save = Path(directory) / "profiles/1000/CUSA09062/savedata/Fixture"
                save.mkdir(parents=True)
                sentinel = save / "existing.bin"
                expected = b"Existing synthetic save must not be hidden or overwritten"
                sentinel.write_bytes(expected)
                result = self.invoke(storage_mount_fixture(mode, 0x809f000f if mode == 1 else 0x809f0007, blocks=96), "--enter", user_directory=directory)
                self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
                self.assertEqual(sentinel.read_bytes(), expected)
                self.assertEqual(list(save.iterdir()), [sentinel])

    def test_save_store_persistence_quota_metadata_search_and_recoverable_deletion(self):
        with tempfile.TemporaryDirectory(prefix='kt-save-store-') as directory:
            result = subprocess.run([str(ROOT / 'out/native/KetsuiNativeProbe.exe'), '--save-store-self-test', directory],
                                    capture_output=True, text=True, timeout=15, cwd=ROOT)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            cases = [event for event in events if event['event'] == 'save_store_self_test_case']
            self.assertEqual(len(cases), 42)
            self.assertTrue(all(event['passed'] for event in cases))

    def test_save_sysv_mount_metadata_file_io_unmount_and_second_process_readback(self):
        with tempfile.TemporaryDirectory(prefix='kt-save-sysv-') as directory:
            first = self.invoke(save_io_fixture(), '--enter', user_directory=directory)
            self.assertEqual(first.returncode, 7, first.stdout + first.stderr)
            slot = Path(directory) / 'profiles/1000/CUSA09062/savedata/Fixture'
            self.assertEqual((slot / 'data.bin').read_bytes(), struct.pack('<QQ', 0x8877665544332211, 0xff00ee11dd22cc33))
            before = {str(path): hashlib.sha256(path.read_bytes()).digest() for path in slot.rglob('*') if path.is_file()}
            second = self.invoke(save_io_fixture(reopen=True), '--enter', user_directory=directory)
            self.assertEqual(second.returncode, 7, second.stdout + second.stderr)
            self.assertEqual(before, {str(path): hashlib.sha256(path.read_bytes()).digest() for path in slot.rglob('*') if path.is_file()})
            events = [json.loads(line) for line in first.stdout.splitlines()]
            self.assertTrue(any(event['event'] == 'native_file_write' and event['written'] == 16 for event in events))
            self.assertTrue(any(event['event'] == 'native_storage_unmount' and event['flushed'] for event in events))

    def test_save_descriptor_exhaustion_never_truncates_existing_payload(self):
        with tempfile.TemporaryDirectory(prefix='kt-save-fd-limit-') as directory:
            result = self.invoke(save_io_fixture(exhaust=True), '--enter', user_directory=directory)
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            data = Path(directory) / 'profiles/1000/CUSA09062/savedata/Fixture/data.bin'
            self.assertEqual(data.read_bytes(), struct.pack('<QQ', 0x8877665544332211, 0xff00ee11dd22cc33))

    def test_storage_mount_wrong_namespace_is_not_resolved(self):
        for library, module in (("fixture", "libSceSaveData"), ("libSceSaveData", "fixture")):
            with self.subTest(library=library, module=module):
                result = self.invoke(synthetic_probe(identifier=nid("sceSaveDataMount2") + "#A#A",
                                                     name="sceSaveDataMount2", library=library, module=module), "--enter")
                self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
                self.assertEqual(json.loads(result.stdout.splitlines()[-1])["event"], "unimplemented_import")

    def test_storage_original_data_guard_does_not_depend_on_cwd(self):
        with tempfile.TemporaryDirectory(prefix="kt-storage-cwd-") as directory:
            result = subprocess.run([str(ROOT / "out/native/KetsuiNativeProbe.exe"), "--storage-self-test",
                                     str(ROOT / "game-data/should-not-create")], cwd=directory,
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn("original game data", result.stderr)

    def test_save_dialog_sysv_lifecycle(self):
        result = self.invoke(save_dialog_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_save_dialog_list_stops_at_unimplemented_feature(self):
        result = self.invoke(save_dialog_fixture(list_mode=True), "--enter")
        self.assertEqual(result.returncode, 20, result.stdout + result.stderr)
        event = json.loads(result.stdout.splitlines()[-1])
        self.assertEqual(event["event"], "unimplemented_api")
        self.assertEqual(event["name"], "sceSaveDataDialogOpen_mode")
        self.assertEqual(event["detail"], "0x1")
        self.assertTrue(event["in_game_image"])

    def test_cxa_finalize_all_integrates_plain_atexit_lifo(self):
        result = self.invoke(cxa_fixture(), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(sum(e["event"] == "cxa_atexit_callback" for e in events), 3)
        self.assertEqual(sum(e["event"] == "atexit_callback" for e in events), 1)

    def test_cxa_finalize_dso_and_remaining_exit(self):
        result = self.invoke(cxa_fixture(scoped=True), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_cxa_finalize_recursion_executes_once(self):
        result = self.invoke(cxa_fixture(recursive=True), "--enter")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(sum(e["event"] == "cxa_atexit_callback" for e in events), 3)

    def test_fault_log_bounds_long_quoted_import_metadata(self):
        result = self.invoke(synthetic_probe(1, name='"\\' * 2048), "--enter")
        self.assertEqual(result.returncode, 22, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(events[-1]["event"], "unimplemented_data_import")
        self.assertEqual(events[-1]["name"], '"\\' * 128 + "...")

    def test_fault_log_captures_bounded_register_instruction_and_stack_candidates_without_unwinding(self):
        asm = SyntheticCode()
        asm.emit('48 83 ec 08 31 ff')
        asm.relative('e8', 'fault')
        asm.label('return_site')
        asm.emit('0f 0b')
        asm.label('fault')
        asm.emit('48 8b 97 88 00 00 00 c3')  # read [null + 0x88], like a null object getter
        result = self.invoke(interfaces_fixture(asm.finish(), [('_Exit', 'libc')]), '--enter')
        self.assertEqual(result.returncode, 22, result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        context = next(e for e in events if e['event'] == 'fault_cpu_context')
        self.assertEqual(context['instruction_bytes'][:7], ['0x48', '0x8b', '0x97', '0x88', '0x0', '0x0', '0x0'])
        self.assertLessEqual(len(context['instruction_bytes']), 32)
        self.assertIn('r15', context)
        chunks = [e for e in events if e['event'] == 'fault_stack_sample']
        self.assertTrue(chunks)
        self.assertLessEqual(sum(len(e['words']) for e in chunks), 128)
        self.assertTrue(all(len(e['words']) <= 16 and not e['unwound'] for e in chunks))
        candidates = [e for e in events if e['event'] == 'fault_stack_image_candidate']
        self.assertTrue(any(e['word_index_hex'] == '0x0' and e['image_offset'] == hex(asm.labels['return_site'])
                            and not e['verified_return_address'] for e in candidates))
        self.assertEqual(events[-1]['event'], 'unhandled_exception')
        self.assertEqual(events[-1]['fault_address'], '0x88')

    def test_trace_child_preserves_exit_and_exception_diagnostics(self):
        for kind, expected in ((2, 20), (1, 22)):
            with self.subTest(kind=kind):
                result = self.invoke(synthetic_probe(kind), "--trace-enter")
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                self.assertIn(f"debug_finished status={expected} timed_out=false", result.stderr)
                if kind == 1:
                    self.assertIn("debug_exception code=0xc0000005", result.stderr)
                    self.assertIn("debug_context reason=exception", result.stderr)

    def test_trace_watchdog_samples_and_terminates_spinning_child(self):
        data = synthetic_probe()
        data[128:130] = b"\xeb\xfe"  # authored infinite loop, no game input
        result = self.invoke(data, "--trace-enter")
        self.assertEqual(result.returncode, 26, result.stdout + result.stderr)
        self.assertIn("debug_context reason=watchdog", result.stderr)
        self.assertIn("debug_finished status=26 timed_out=true", result.stderr)

    @unittest.skipUnless(shutil.which("pwsh"), "PowerShell 7 is required for the run wrapper")
    @unittest.skipUnless((ROOT / 'out/gpu-core/KetsuiGpuRuntime.dll').is_file(), 'Build resident GPU runtime first')
    def test_run_wrapper_resolves_relative_report_root_before_launching_child(self):
        with tempfile.TemporaryDirectory(prefix='kt-wrapper-relative-') as directory:
            root = Path(directory)
            image = root / 'synthetic.ktprobe'
            data = synthetic_probe()
            image.write_bytes(data)
            source = root / 'synthetic-source.bin'
            source.write_bytes(b'Authored wrapper path fixture, not game code')
            image.with_suffix('.json').write_text(json.dumps({
                'sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                'probe_sha256': hashlib.sha256(data).hexdigest(), 'contracts': [],
            }), encoding='utf-8')
            relative = Path('报告 nested') / 'runs'
            result = subprocess.run([shutil.which('pwsh'), '-NoProfile', '-File',
                                     str(ROOT / 'scripts/Run-NativeProbe.ps1'),
                                     '-InputFile', str(image), '-SourceFile', str(source),
                                     '-ReportRoot', str(relative), '-UserDirectory', str(root / 'profile'),
                                     '-Enter', '-GpuBackend', 'resident'],
                                    capture_output=True, text=True, timeout=30, cwd=root)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            reports = list((root / relative).glob('native-*'))
            self.assertEqual(len(reports), 1)
            record = json.loads((reports[0] / 'run.json').read_text(encoding='utf-8-sig'))
            self.assertEqual(record['exitCode'], 20, 'Authored unresolved import must remain diagnostic')
            self.assertFalse(record['timedOut'])
            self.assertTrue(record['sourceUnchanged'])
            snapshot = Path(record['inputConfig']['snapshot'])
            self.assertTrue(snapshot.is_absolute())
            self.assertEqual(snapshot, reports[0] / 'binary/input.ini')
            self.assertTrue((reports[0] / 'binary/KetsuiGpuRuntime.dll').is_file())
            self.assertNotIn('must be an existing absolute file path', result.stderr)

    @unittest.skipUnless(shutil.which("pwsh"), "PowerShell 7 is required for the run wrapper")
    def test_run_wrapper_drains_large_stdout_without_event_pump(self):
        with tempfile.TemporaryDirectory(prefix="kt-wrapper-") as directory:
            root = Path(directory)
            image = root / "synthetic.ktprobe"
            data = synthetic_probe(name="a" * 4096)
            image.write_bytes(data)
            source = root / "synthetic-source.bin"
            source.write_bytes(b"Synthetic test input, not game code")
            image.with_suffix(".json").write_text(json.dumps({
                "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                "probe_sha256": hashlib.sha256(data).hexdigest(), "contracts": [],
            }), encoding="utf-8")
            result = subprocess.run([shutil.which("pwsh"), "-NoProfile", "-File",
                                     str(ROOT / "scripts/Run-NativeProbe.ps1"),
                                     "-InputFile", str(image), "-SourceFile", str(source),
                                     "-ReportRoot", str(root / "reports"), "-Enter"],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            reports = list((root / "reports").glob("native-*"))
            self.assertEqual(len(reports), 1)
            record = json.loads((reports[0] / "run.json").read_text(encoding="utf-8-sig"))
            self.assertFalse(record["timedOut"])
            self.assertEqual(record["exitCode"], 20)
            self.assertTrue(record["sourceUnchanged"])
            manifest_path = reports[0] / "binary/build.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
            self.assertEqual(record["nativeBuildId"], manifest["buildId"])
            self.assertEqual(record["buildManifestSha256"].lower(),
                             hashlib.sha256(manifest_path.read_bytes()).hexdigest())
            for binary in manifest["binaries"]:
                self.assertEqual(binary["sha256"],
                                 hashlib.sha256((reports[0] / "binary" / binary["name"]).read_bytes()).hexdigest())
            events = [json.loads(line) for line in (reports[0] / "stdout.jsonl").read_text().splitlines()]
            self.assertEqual(events[-1]["name"], "a" * 4096)


if __name__ == "__main__":
    unittest.main()
