"""Bounded PS4/M2 network replay framing shared by title-specific profiles."""
import hashlib
import struct
import zlib

from .games.ketsui import require, ApiError, MAX_REPLAY


def inflate(payload, limit):
    require(0 < len(payload) <= MAX_REPLAY, 'invalid_replay_size')
    decoder = zlib.decompressobj()
    try:
        plain = decoder.decompress(payload, limit + 1)
    except zlib.error:
        raise ApiError(400, 'invalid_replay_compression') from None
    require(len(plain) <= limit and decoder.eof and not decoder.unused_data and not decoder.unconsumed_tail,
            'invalid_replay_compression')
    return plain


def network_record(payload, *, versions, data_versions, regions, characters):
    # Struct fields and key framing, not a claim of native state compatibility.
    raw = inflate(payload, 0x80000)
    require(len(raw) == 0x80000, 'invalid_replay_size')
    version, core, extent, screen, total, consumed, snapshot, players, mask, pages = struct.unpack_from('<10I', raw)
    require(version in versions and core in data_versions and extent <= 0x78000 and screen == 0,
            'invalid_replay_version')
    require(total and not total % 512 and 0 < consumed <= total and 0 < snapshot <= 0x78000 and
            1 <= players <= 8 and mask, 'invalid_replay_header')
    stride = total // 512
    lines = players * mask.bit_count()
    directory_bytes = stride * lines
    remaining = 0x78000 - snapshot
    require(directory_bytes < remaining and pages < (remaining - directory_bytes) // 8, 'invalid_replay_keys')
    directory = raw[0x500 + snapshot:0x500 + snapshot + directory_bytes]
    require(sum(value.bit_count() for value in directory) == pages, 'invalid_replay_keys')
    occupied = []
    for line in range(lines):
        for index, value in enumerate(directory[line * stride:(line + 1) * stride]):
            while value:
                bit = (value & -value).bit_length() - 1
                occupied.append((index * 8 + bit, line))
                value &= value - 1
    words = 0x500 + snapshot + directory_bytes
    for index, (page, line) in enumerate(sorted(occupied)):
        require(page * 64 < consumed, 'invalid_replay_keys')
        word = struct.unpack_from('<Q', raw, words + index * 8)[0]
        frames = min(64, consumed - page * 64)
        require(word and (frames == 64 or not word >> frames), 'invalid_replay_keys')
    state = inflate(raw[0x500:0x500 + snapshot], 2 * 1024 * 1024)
    cursor = 0
    names = set()
    while cursor < len(state):
        length = state[cursor]
        cursor += 1
        require(length and len(state) - cursor >= length + 4 and len(names) < 4096, 'invalid_replay_snapshot')
        name = state[cursor:cursor + length]
        cursor += length
        require(all(0x20 <= c <= 0x7e for c in name) and name not in names, 'invalid_replay_snapshot')
        names.add(name)
        size = struct.unpack_from('<I', state, cursor)[0]
        cursor += 4
        require(size <= len(state) - cursor, 'invalid_replay_snapshot')
        cursor += size
    require(names, 'invalid_replay_snapshot')

    def text(offset, size):
        value = raw[offset:offset + size]
        require(0 in value, 'invalid_replay_text')
        return value.split(b'\0', 1)[0]

    require(text(48, 16), 'invalid_replay_title')
    region = text(80, 16)
    require(region in regions, 'invalid_replay_core')
    text(120, 17)
    text(137, 17)
    kind, mode, score, character, stage = struct.unpack_from('<HHIHH', raw, 104)
    require(kind == 0 and character in characters, 'invalid_replay_metadata')
    return dict(score=score, character=character, mode=mode, stage=stage, region=region.decode(),
                version=version, data_version=core, consumed_frames=consumed,
                record_sha256=hashlib.sha256(raw).hexdigest())
