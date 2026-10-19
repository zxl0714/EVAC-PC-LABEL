"""SDOJ native score/replay adapter, using the shared bounded HTTP/SQLite store."""
from dataclasses import dataclass
import struct

from .espgaluda2 import ApiError, Game, Store as NativeStore, MAX_REPLAY, serve

# Reviewed disc-base selector 82124018. Rows are characters; columns are
# Shot/Laser/Expert. Xbox 360 mode uses Saya independently of its character arg.
VIEW_TABLE = {
    2: ((2, 7, 11), (3, 9, 12), (4, 10, 13), (85, 86, 87)),
    3: ((23, 24, 25),),
    4: ((14, 17, 20), (15, 18, 21), (16, 19, 22), (88, 89, 90)),
    5: ((35, 38, 41), (36, 39, 42), (37, 40, 43), (91, 92, 93)),
}
VIEW_CHARACTER_TYPE = {view: ((3 if mode == 3 else character) << 4) | dress
                       for mode, rows in VIEW_TABLE.items()
                       for character, row in enumerate(rows) for dress, view in enumerate(row)}
SDOJ = Game('sdoj', '435A07E7', frozenset(VIEW_CHARACTER_TYPE))
REPLAY_FORMAT = 'sdoj-disc-native-v1'
REPLAY_HEADER = 304
REPLAY_MAGIC = 0x01304250
PROPERTY_TYPES = {0x20000004: 2, 0x20000002: 2, 0x10000007: 1,
                  0x10000006: 1, 0x10000005: 1, 0x10000001: 1,
                  0x10008011: 1}
REQUIRED_PROPERTIES = frozenset(PROPERTY_TYPES) - {0x10008011}


def filetime(native_time):
    # XDK FILETIME is low DWORD followed by high DWORD. The title copied both
    # as a single 64-bit property, which may be negative. Preserve that property
    # verbatim; swap the DWORD order only when interpreting the recording time.
    bits = native_time & ((1 << 64) - 1)
    result = ((bits & 0xffffffff) << 32) | (bits >> 32)
    if not 0 < result < 1 << 63:
        raise ApiError(400, 'invalid_native_time')
    return result


@dataclass(frozen=True)
class ReplayInfo:
    score: int
    timestamp: int
    character_type: int
    stage: int
    continues: int
    option: int


def replay_info(data):
    if not REPLAY_HEADER < len(data) <= MAX_REPLAY or len(data) % 2:
        raise ApiError(400, 'invalid_replay_size')
    magic, size = struct.unpack_from('>I', data, 0)[0], struct.unpack_from('>I', data, 92)[0]
    if magic != REPLAY_MAGIC or size != len(data):
        raise ApiError(400, 'invalid_replay_envelope')
    # Native load path 8212EFC8 verifies a weighted big-endian halfword sum.
    # The checksum is the halfword at +8 (weight 5), omitted from the sum.
    words = struct.iter_unpack('>H', data)
    checksum = sum(value * (index + 1) for index, (value,) in enumerate(words) if index != 4) & 0xffff
    if struct.unpack_from('>H', data, 8)[0] != checksum:
        raise ApiError(400, 'invalid_replay_checksum')
    score = struct.unpack_from('>q', data, 24)[0]
    timestamp = struct.unpack_from('>q', data, 32)[0]
    character_type, stage, continues = struct.unpack_from('>III', data, 52)
    option = struct.unpack_from('>I', data, 76)[0]
    if (score < 0 or character_type not in set(VIEW_CHARACTER_TYPE.values())
            or any(value > 0xffff for value in (stage, continues, option))):
        raise ApiError(400, 'invalid_replay_metadata')
    filetime(timestamp)
    return ReplayInfo(score, timestamp, character_type, stage, continues, option)


def match_replay(data, properties):
    info = replay_info(data)
    actual = {p['id']: (p['type'], int(p['value'])) for p in properties}
    expected = {0x20000004: (2, info.score), 0x20000002: (2, info.timestamp),
                0x10000007: (1, info.character_type), 0x10000006: (1, info.stage),
                0x10000005: (1, info.continues), 0x10000001: (1, info.option)}
    if any(actual.get(key) != value for key, value in expected.items()):
        raise ApiError(409, 'native_replay_mismatch')
    # Native score writes clear the storage-size flag before upload. The
    # confirmed attachment, not this original property, advertises availability.
    if 0x10008011 in actual and actual[0x10008011] not in ((1, 0), (1, len(data))):
        raise ApiError(409, 'native_replay_mismatch')


class Store(NativeStore):
    score_property = 0x20000004
    replay_format = REPLAY_FORMAT
    inspect_record = staticmethod(replay_info)
    match_record = staticmethod(match_replay)

    def __init__(self, database):
        super().__init__(database, SDOJ)

    def validate_properties(self, view, props, score):
        actual = {p['id']: p for p in props}
        if not REQUIRED_PROPERTIES <= actual.keys() or actual.keys() - PROPERTY_TYPES.keys():
            raise ApiError(400, 'invalid_native_properties')
        if any(p['type'] != PROPERTY_TYPES[p['id']] for p in props):
            raise ApiError(400, 'invalid_native_property_type')
        if int(actual[0x10000007]['value']) != VIEW_CHARACTER_TYPE[view]:
            raise ApiError(400, 'native_view_mismatch')
        for key in (0x10000001, 0x10000005, 0x10000006):
            if not 0 <= int(actual[key]['value']) <= 0xffff:
                raise ApiError(400, 'invalid_native_metadata')
        if 0x10008011 in actual:
            size = int(actual[0x10008011]['value'])
            if size and not REPLAY_HEADER < size <= MAX_REPLAY:
                raise ApiError(400, 'invalid_native_replay_size')
        filetime(int(actual[0x20000002]['value']))


def main():
    serve(Store, 18087)


if __name__ == '__main__':
    main()
