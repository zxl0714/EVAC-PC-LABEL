"""Guwange XBLA base native boards and byte-preserving recording transport."""
from dataclasses import dataclass
import struct

from .espgaluda2 import ApiError, Game, Store as NativeStore, MAX_REPLAY, serve

GUWANGE = Game('guwange', '58410A78', range(1, 13))
REPLAY_FORMAT = 'guwange-base-native-v1'
REPLAY_HEADER = 256
FIXED_PAYLOAD = 0x2cab8
MAGIC = tuple(f'GWN_for_XB360_{mode}20100820'.encode() for mode in ('AC', 'BL', 'XB'))
PROPERTY_TYPES = {0x20000004: 2, 0x10000008: 1, 0x10000006: 1, 0x10000001: 1,
                  0x10000003: 1, 0x20000002: 2, 0x10000005: 1, 0x10008011: 1}
REQUIRED_PROPERTIES = frozenset(PROPERTY_TYPES) - {0x10008011}


@dataclass(frozen=True)
class ReplayInfo:
    filetime: int
    mode: int
    play_kind: int
    stage: int
    players: tuple


def replay_info(data):
    if not REPLAY_HEADER + FIXED_PAYLOAD <= len(data) <= MAX_REPLAY:
        raise ApiError(400, 'invalid_replay_size')
    payload, filetime, kind, mode, play_kind = struct.unpack_from('>IQIII', data, 36)
    # Native selected play kind: ordinary Play (0) or Score Attack (1).
    if (payload != len(data) - REPLAY_HEADER or kind != 2 or mode not in range(3)
            or play_kind not in (0, 1) or not 0 < filetime < 1 << 63):
        raise ApiError(400, 'invalid_replay_envelope')
    # Native strcmp ignores nonzero padding after the terminator.
    if data[:32].split(b'\0', 1)[0] != MAGIC[mode]:
        raise ApiError(400, 'invalid_replay_magic')
    checksum = sum(v if v < 128 else v - 256 for v in data[REPLAY_HEADER:]) & 0xffff
    if checksum != struct.unpack_from('>H', data, 32)[0]:
        raise ApiError(400, 'invalid_replay_checksum')
    stage = struct.unpack_from('>h', data, 62)[0]
    if stage >= 5:
        stage -= 1
    players = tuple(struct.unpack_from('>II', data, offset) for offset in (76, 104))
    if (any(character not in (0, 1, 2, 0xffffffff) for character, _ in players)
            or all(character == 0xffffffff for character, _ in players)):
        raise ApiError(400, 'invalid_replay_players')
    return ReplayInfo(filetime, mode, play_kind, stage, players)


class Store(NativeStore):
    score_property = 0x20000004
    replay_format = REPLAY_FORMAT
    inspect_record = staticmethod(replay_info)

    def __init__(self, database):
        super().__init__(database, GUWANGE)

    def validate_properties(self, view, props, score):
        actual = {p['id']: p for p in props}
        if not REQUIRED_PROPERTIES <= actual.keys() or actual.keys() - PROPERTY_TYPES.keys():
            raise ApiError(400, 'invalid_native_properties')
        if any(p['type'] != PROPERTY_TYPES[p['id']] for p in props):
            raise ApiError(400, 'invalid_native_property_type')
        character = int(actual[0x10000008]['value'])
        slot = (view - 1) % 4
        if character not in range(3) or (slot and character != slot - 1):
            raise ApiError(400, 'native_view_mismatch')
        # The native score writer swaps the two FILETIME words before
        # storing this signed property. Preserve it; only decode to validate.
        packed_time = int(actual[0x20000002]['value']) & ((1 << 64) - 1)
        score_time = (packed_time & 0xffffffff) << 32 | packed_time >> 32
        if (not 0 <= score <= 0x7fffffff or
                not -32768 <= int(actual[0x10000006]['value']) <= 32767 or
                any(int(actual[key]['value']) != 0 for key in (0x10000001, 0x10000003, 0x10000005)) or
                not 0 < score_time < 1 << 63):
            raise ApiError(400, 'invalid_native_metadata')
        size = int(actual.get(0x10008011, {'value': 0})['value'])
        if size and not REPLAY_HEADER + FIXED_PAYLOAD <= size <= MAX_REPLAY:
            raise ApiError(400, 'invalid_native_replay_size')

    def match_view_record(self, view, data, properties):
        info = replay_info(data)
        values = {p['id']: int(p['value']) for p in properties}
        if (info.mode != (view - 1) // 4 or info.stage != values[0x10000006] or
                (values[0x10000008], values[0x20000004]) not in info.players or
                values.get(0x10008011, 0) not in (0, len(data))):
            raise ApiError(409, 'native_replay_mismatch')
        # Score and recording timestamps come from separate original calls.
        # No timestamp rewrite or artificial equality requirement.


def main():
    serve(Store, 18090)


if __name__ == '__main__':
    main()
