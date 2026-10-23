"""Authored envelope bytes for protocol tests; not a playable game recording."""
import struct
import zlib


def record(game):
    if game == 'daioujou':
        size, version, data_version, region = 0x80000, 0x10030, 0x10000, b'ARCADE_01030'
    elif game == 'ketsui':
        size, version, data_version, region = 0x80000, 0x10001, 0x10001, b'ARCADE_01000'
    elif game == 'esprade':
        size, version, data_version, region = 0x80000, 0x10022, 0x10000, b'ARCADE_01030'
    else:
        raise ValueError('Unknown authored replay profile')
    data = bytearray(size)
    snapshot = zlib.compress(bytes([8]) + b'AUTHORED' + struct.pack('<I', 7) + b'fixture')
    struct.pack_into('<10I', data, 0, version, data_version, 0, 0, 1024, 600, len(snapshot), 1, 1, 0)
    data[48:55] = b'fixture'
    data[80:80 + len(region)] = region
    struct.pack_into('<IHH', data, 108, 12345, 0, 1)
    data[0x500:0x500 + len(snapshot)] = snapshot
    return data
