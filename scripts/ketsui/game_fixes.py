"""Prepare source-free, hash-bound read overlays without modifying game data.

PSB/MDF layout reference: FreeMote d87be1f3b8458c81822825fd6d56123d4df860b4.
Archive key bytes are read from the verified local executable, not shipped here.
Generated overlays contain derived game data and belong outside the Git tree.
"""
import hashlib
from pathlib import Path, PurePosixPath
import random
import re
import struct
import zlib

from ps4.native_elf import Elf

MAGIC = b'KTFIX01\0'
MAX_FILE = 4 * 1024 * 1024
EBOOT_SHA256 = 'a32b4b798cc09d8b3f756acde33d679dbb7fec1a54eec50cc179f3f3694ac073'
BASE_EBOOT_SHA256 = '22927d470a261d52c31510b8e5f173f4f6256307828e302f6eab19f4357c0e8f'
DIRECT_MENU_SHA256 = '83ba95c0ff110a17f9e641addc59e61b89c05431e57a91235eb15c872045f672'
DIRECT_MENU = '5000/script/title_mode_direct_menu.nut.m'
RANKING_MANAGER = 'system/script/ranking_manager.nut.m'
RANKING_MANAGER_SHA256 = '8826a4a6545d302df876f9096a15147a036098d724d2fbd6c7f6a2349f972e43'
TITLE_STANDALONE = '5000/script/title_standalone.nut.m'
TITLE_STANDALONE_SHA256 = '889ac1152f99d774125e199179264887aa8a21986f670e5c48fc79d0c099db9d'
BASE_TITLE_STANDALONE_SHA256 = '63b6a45d85e49936665e82e24f188e64fca4e60bc9bc850e71979ca2b9ef6bc8'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_virtual(elf, address, size):
    segments = [s for s in elf.segments if s['address'] <= address and
                size <= s['file_size'] and address - s['address'] <= s['file_size'] - size]
    if len(segments) != 1:
        raise ValueError('Archive initialization data is not in one file-backed segment')
    return elf.slice(segments[0]['file_offset'] + address - segments[0]['address'], size)


def archive_key(executable):
    source = digest(executable)
    if source not in (EBOOT_SHA256, BASE_EBOOT_SHA256):
        raise ValueError('Game fixes require a verified CUSA09062 executable')
    load = 0xd2 if source == BASE_EBOOT_SHA256 else 0xd3
    elf = Elf(executable)
    if read_virtual(elf, load, 2) != b'\x48\xb8' or read_virtual(elf, load+10, 2) != b'\x48\xb9':
        raise ValueError('Archive initialization instruction layout changed')
    # Two overlapping eight-byte immediate stores construct a 13-byte seed.
    return read_virtual(elf, load+12, 8) + read_virtual(elf, load+5, 5)


def mask_for(key, name):
    seed = hashlib.md5(key + name.encode('utf-8')).digest()
    generator = random.Random(int.from_bytes(seed, 'little'))
    return b''.join(struct.pack('<I', generator.getrandbits(32)) for _ in range(16))


def unwrap(raw, key, name):
    if len(raw) > 32 * 1024 * 1024:
        raise ValueError('Oversized archive resource')
    if not raw.startswith(b'mdf\0'):
        return raw
    if len(raw) < 8:
        raise ValueError('Truncated MDF header')
    expected, = struct.unpack_from('<I', raw, 4)
    if expected > 32 * 1024 * 1024:
        raise ValueError('Oversized MDF output')
    mask = mask_for(key, name)
    encoded = bytes(value ^ mask[index % 64] for index, value in enumerate(raw[8:]))
    inflater = zlib.decompressobj()
    output = inflater.decompress(encoded, expected + 1)
    if len(output) != expected or not inflater.eof or inflater.unconsumed_tail or any(inflater.unused_data):
        raise ValueError('Invalid MDF output extent or trailing data')
    return output


def wrap_fixed(raw, key, name, extent):
    compressed = zlib.compress(raw, 9)
    if len(compressed) + 8 > extent:
        raise ValueError('Fixed-size resource overlay does not fit the original extent')
    payload = compressed + bytes(extent - 8 - len(compressed))
    mask = mask_for(key, name)
    result = b'mdf\0' + struct.pack('<I', len(raw))
    result += bytes(value ^ mask[index % 64] for index, value in enumerate(payload))
    if unwrap(result, key, name) != raw:
        raise ValueError('Prepared resource failed its decode roundtrip')
    return result


class Psb:
    def __init__(self, raw):
        if len(raw) < 40 or raw[:8] != b'PSB\0\2\0\0\0':
            raise ValueError('Expected a PSB v2 index')
        self.raw = raw
        self.header = struct.unpack_from('<8I', raw, 8)
        charset, at = self.array(self.header[1])
        parents, at = self.array(at)
        terminals, _ = self.array(at)
        self.names = []
        for terminal in terminals:
            if terminal >= len(parents):
                raise ValueError('PSB name terminal out of range')
            chars, node = [], parents[terminal]
            while node:
                if node >= len(parents) or len(chars) > 4096:
                    raise ValueError('Invalid PSB name chain')
                parent = parents[node]
                if parent >= len(charset) or not 0 <= node - charset[parent] <= 255:
                    raise ValueError('Invalid PSB name character')
                chars.append(node - charset[parent])
                node = parent
            self.names.append(bytes(reversed(chars)).decode('utf-8'))
        self.strings, _ = self.array(self.header[2])
        self.nodes = 0
        self.root = self.value(self.header[7])

    def number(self, at, size, signed=False):
        if at < 0 or not 1 <= size <= 8 or at > len(self.raw) - size:
            raise ValueError('Truncated PSB integer')
        return int.from_bytes(self.raw[at:at + size], 'little', signed=signed)

    def array(self, at):
        width = self.number(at, 1) - 12
        count = self.number(at + 1, width)
        at += 1 + width
        item_width = self.number(at, 1) - 12
        at += 1
        if not 1 <= item_width <= 8 or count > 1000000 or at > len(self.raw) - count * item_width:
            raise ValueError('Invalid PSB array extent')
        return [self.number(at + i * item_width, item_width) for i in range(count)], at + count * item_width

    def value(self, at, depth=0):
        self.nodes += 1
        if depth > 64 or self.nodes > 1000000:
            raise ValueError('PSB recursion bound')
        kind = self.number(at, 1)
        at += 1
        if kind in (0, 1):
            return None
        if kind in (2, 3):
            return kind == 3
        if kind == 4:
            return 0
        if 5 <= kind <= 12:
            return self.number(at, kind - 4, True)
        if 13 <= kind <= 20:
            return self.array(at - 1)[0]
        if 21 <= kind <= 24:
            index = self.number(at, kind - 20)
            if index >= len(self.strings):
                raise ValueError('PSB string index out of range')
            start = self.header[3] + self.strings[index]
            if start >= len(self.raw):
                raise ValueError('PSB string offset out of range')
            return self.raw[start:self.raw.index(b'\0', start)].decode('utf-8')
        if 25 <= kind <= 28:
            return dict(resource_index=self.number(at, kind - 24))
        if kind == 29:
            return 0.0
        if kind in (30, 31):
            size = 4 if kind == 30 else 8
            self.number(at, size)
            return struct.unpack_from('<f' if kind == 30 else '<d', self.raw, at)[0]
        if kind == 32:
            offsets, at = self.array(at)
            return [self.value(at + offset, depth + 1) for offset in offsets]
        if kind == 33:
            names, at = self.array(at)
            offsets, at = self.array(at)
            if len(names) != len(offsets) or len(set(names)) != len(names) or any(i >= len(self.names) for i in names):
                raise ValueError('Invalid PSB object keys')
            return {self.names[name]: self.value(at + offset, depth + 1) for name, offset in zip(names, offsets)}
        raise ValueError('Unsupported PSB value')


def fix_direct_menu(raw):
    anchor = b'm_bgm_mode[_gamemode] = _bgmMode;'
    # Normal BGM panel and CUSTOM's combined BGM/base-settings panel.
    if raw.count(anchor) != 2:
        raise ValueError('Expected exactly two music confirmation paths')
    newline = b'\r\n' if b'\r\n' in raw else b'\n'
    addition = newline.join((anchor,
        b'        ::g_systemdata.get_value(SystemDataValueIndex.SETTING_SOUND).set_bgm_set_mode(_gamemode, _bgmMode);',
        b'        ::g_systemdata.CheckValueChangeAutosave(SystemDataValueIndex.SETTING_SOUND);',
        b'        this._check_autosave();'))
    fixed = raw.replace(anchor, addition)
    # Only whole-line comments are removed from this hash-verified script.
    # Strings, inline comments, block comments and executable lines stay intact.
    return b''.join(line for line in fixed.splitlines(keepends=True) if not line.lstrip().startswith(b'//'))


def fix_ranking_replay_batch(raw):
    newline = b'\r\n' if b'\r\n' in raw else b'\n'
    begin = b'function RankingManager::_recordEntryBody('
    end = b'function friend_ranking_sort_func('
    if raw.count(begin) != 1 or raw.count(end) != 1:
        raise ValueError('Expected one ranking submission batch boundary')
    start, stop = raw.index(begin), raw.index(end)
    if stop <= start:
        raise ValueError('Invalid ranking submission batch order')
    body = raw[start:stop]
    score = b'  local result = this._recordScore(_boardIdArray, _entryInfo);'
    failure = newline.join((b'          if ( !this.isSucceeded(result) ) ', b'            break;'))
    returned = b'  return result;'
    if any(body.count(anchor) != 1 for anchor in (score, failure, returned)):
        raise ValueError('Ranking batch differs from the verified control flow')
    body = body.replace(score, score + newline + b'  local first_transfer_error = null;')
    body = body.replace(failure, newline.join((
        b'          if ( !this.isSucceeded(result) && first_transfer_error == null )',
        b'            first_transfer_error = result;')))
    body = body.replace(returned, b'  return first_transfer_error != null ? first_transfer_error : result;')
    fixed = raw[:start] + body + raw[stop:]
    return b''.join(line for line in fixed.splitlines(keepends=True) if not line.lstrip().startswith(b'//'))


def fix_community_local_round_gate(raw):
    begin = b'  function checkRankingAndRegistRanking() {'
    end = b'  function checkRegistRanking() {'
    if raw.count(begin) != 1 or raw.count(end) != 1 or raw.index(end) <= raw.index(begin):
        raise ValueError('Expected one post-game ranking submission boundary')
    start, stop = raw.index(begin), raw.index(end)
    body = raw[start:stop]
    gate = b'      if (ranking_is_network_ok_with_sign_in() && !_net.getUgcRestricted()) {'
    submission = b'local _ret = ::g_ranking_ctrl.registRankingAttackHighscore(m_replay_score, _ship);'
    initializers = (b'initRanking(this, true, this.TYPE_MODE_ARRAY_DEFAULT[GameMode.ETC + _round_type]);',
                    b'initRanking(this, true, this.TYPE_MODE_ARRAY_DEFAULT[_gamemode]);')
    if body.count(gate) != 1 or body.count(b'if (checkRegistRanking()) {') != 1 or body.count(submission) != 2 or any(body.count(item) != 1 for item in initializers):
        raise ValueError('Post-game ranking submission differs from verified control flow')
    # The explicit community producer uses its initialized local backend, not
    # Sony sign-in/UGC state (which the original game also marks restricted on
    # NP service errors). Do not change those shared states or query callers.
    # Native -CommunityRounds still validates and owns every captured record.
    body = body.replace(gate, b'      if (::g_ranking_manager.isInitialized()) {')
    # initRanking already has a local-initialization switch. Use it only at
    # these two post-game producers; don't change its default or query callers.
    for initializer in initializers:
        body = body.replace(initializer, initializer[:-2] + b', LEADERBOARD_PLAYER_FILTER_MINE, true);')
    # The resource index fixes this script's packed extent. Drop whole-line
    # comments only inside the changed method; leave other source bytes alone.
    body = b''.join(line for line in body.splitlines(keepends=True) if not line.lstrip().startswith(b'//'))
    return raw[:start] + body + raw[stop:]


def encode_read_patches(title, version, patches):
    if not re.fullmatch(r'CUSA[0-9]{5}', title) or not re.fullmatch(r'[0-9]{2}\.[0-9]{2}', version):
        raise ValueError('Invalid read-overlay application identity')
    if not 1 <= len(patches) <= 16:
        raise ValueError('Invalid read-overlay count')
    title, version = title.encode('ascii'), version.encode('ascii')
    output = bytearray(MAGIC + struct.pack('<4I', len(patches), len(title), len(version), 0) + title + version)
    previous = None
    for patch in sorted(patches, key=lambda p: (p['path'], p['offset'])):
        name = patch['path']
        if not name or len(name.encode('utf-8')) > 256 or '\\' in name or ':' in name or '\0' in name or name.startswith('/') or any(x in ('', '.', '..') for x in name.split('/')):
            raise ValueError('Invalid read-overlay relative path')
        if str(PurePosixPath(name)) != name:
            raise ValueError('Noncanonical read-overlay path')
        replacement = bytes(patch['replacement'])
        size, offset = patch['file_size'], patch['offset']
        if type(size) is not int or type(offset) is not int or not 0 <= offset <= size <= (1 << 63) - 1 or not 1 <= len(replacement) <= 262144 or len(replacement) > size - offset:
            raise ValueError('Read-overlay range exceeds original file')
        if previous and previous[0] == name and (previous[1] > offset or previous[2] != size):
            raise ValueError('Overlapping or inconsistent read overlays')
        expected = bytes.fromhex(patch['expected_sha256'])
        if len(expected) != 32:
            raise ValueError('Invalid original read-overlay hash')
        encoded_name = name.encode('utf-8')
        output += (struct.pack('<IIQQ', len(encoded_name), len(replacement), size, offset) + expected +
                   hashlib.sha256(replacement).digest() + encoded_name + replacement)
        previous = name, offset + len(replacement), size
    if len(output) > MAX_FILE:
        raise ValueError('Oversized read-overlay file')
    return bytes(output)


def prepare_music_fix(game_data, *, community_replay=False, community_offline=False):
    community_replay = community_replay or community_offline
    root = Path(game_data)
    executable = (root / 'eboot.bin').read_bytes()
    key = archive_key(executable)
    source_hash = digest(executable)
    updated = source_hash == EBOOT_SHA256
    version = '01.02' if updated else '01.00'
    index_name = 'ps4data_info.psb.m'
    index = Psb(unwrap((root / index_name).read_bytes(), key, index_name)).root
    offset, size = index['file_info'][DIRECT_MENU]
    body = root / 'ps4data_body.bin'
    original_size = body.stat().st_size
    if type(offset) is not int or type(size) is not int or not 0 <= offset <= original_size or not 1 <= size <= 262144 or size > original_size - offset:
        raise ValueError('Music script exceeds original archive')
    with body.open('rb') as source:
        source.seek(offset)
        packed = source.read(size)
    script = unwrap(packed, key, Path(DIRECT_MENU).name)
    if digest(script) != DIRECT_MENU_SHA256:
        raise ValueError('Music script differs from the verified input')
    fixed = fix_direct_menu(script)
    replacement = wrap_fixed(fixed, key, Path(DIRECT_MENU).name, size)
    patch = dict(path='ps4data_body.bin', file_size=original_size, offset=offset,
                 expected_sha256=digest(packed), replacement=replacement)
    patches = [patch]
    community_report = None
    offline_report = None
    if community_replay:
        replay_offset, replay_size = index['file_info'][RANKING_MANAGER]
        if type(replay_offset) is not int or type(replay_size) is not int or not 0 <= replay_offset <= original_size or not 1 <= replay_size <= 262144 or replay_size > original_size - replay_offset:
            raise ValueError('Ranking script exceeds original archive')
        with body.open('rb') as source:
            source.seek(replay_offset)
            replay_packed = source.read(replay_size)
        replay_script = unwrap(replay_packed, key, Path(RANKING_MANAGER).name)
        if digest(replay_script) != RANKING_MANAGER_SHA256:
            raise ValueError('Ranking script differs from the verified input')
        replay_fixed = fix_ranking_replay_batch(replay_script)
        replay_replacement = wrap_fixed(replay_fixed, key, Path(RANKING_MANAGER).name, replay_size)
        patches.append(dict(path='ps4data_body.bin', file_size=original_size, offset=replay_offset,
                            expected_sha256=digest(replay_packed), replacement=replay_replacement))
        community_report = dict(resource=RANKING_MANAGER, offset=replay_offset, bytes=replay_size,
                                original_script_sha256=RANKING_MANAGER_SHA256,
                                fixed_script_sha256=digest(replay_fixed))
    if community_offline:
        patch_index_name = 'ps4data_patch_info.psb.m'
        patch_index = (Psb(unwrap((root / patch_index_name).read_bytes(), key, patch_index_name)).root if updated else index)['file_info']
        title_offset, title_size = patch_index[TITLE_STANDALONE]
        archive = 'ps4data_patch_body.bin' if updated else 'ps4data_body.bin'
        patch_body = root / archive
        patch_size = patch_body.stat().st_size
        if type(title_offset) is not int or type(title_size) is not int or not 0 <= title_offset <= patch_size or not 1 <= title_size <= 262144 or title_size > patch_size - title_offset:
            raise ValueError('Patched title script exceeds original archive')
        with patch_body.open('rb') as source:
            source.seek(title_offset)
            title_packed = source.read(title_size)
        title_script = unwrap(title_packed, key, Path(TITLE_STANDALONE).name)
        title_hash = TITLE_STANDALONE_SHA256 if updated else BASE_TITLE_STANDALONE_SHA256
        if digest(title_script) != title_hash:
            raise ValueError('Title submission script differs from the verified version')
        title_fixed = fix_community_local_round_gate(title_script)
        title_replacement = wrap_fixed(title_fixed, key, Path(TITLE_STANDALONE).name, title_size)
        patches.append(dict(path=archive, file_size=patch_size, offset=title_offset,
                            expected_sha256=digest(title_packed), replacement=title_replacement))
        offline_report = dict(resource=TITLE_STANDALONE, layer='patch' if updated else 'base', offset=title_offset, bytes=title_size,
                              original_script_sha256=title_hash, fixed_script_sha256=digest(title_fixed),
                              scope='eligible-post-game-submission', requires_native_rounds=True)
    output = encode_read_patches('CUSA09062', version, patches)
    if digest((root / 'eboot.bin').read_bytes()) != source_hash or body.stat().st_size != original_size:
        raise ValueError('Game input changed during fix preparation')
    for item in patches:
        original_body = root / item['path']
        if original_body.stat().st_size != item['file_size']:
            raise ValueError('Original resource archive changed during fix preparation')
        with original_body.open('rb') as source:
            source.seek(item['offset'])
            if digest(source.read(len(item['replacement']))) != item['expected_sha256']:
                raise ValueError('Original resource changed during fix preparation')
    report = dict(format=1, fix='direct-menu-music-autosave', title_id='CUSA09062', app_version=version,
        executable_sha256=source_hash, resource=DIRECT_MENU, offset=offset, bytes=size,
        original_file_bytes=original_size, original_packed_sha256=patch['expected_sha256'],
        original_script_sha256=DIRECT_MENU_SHA256, fixed_script_sha256=digest(fixed),
        read_overlay_sha256=digest(output), original_files_modified=False)
    if community_report is not None:
        report['community_replay_batch'] = community_report
    if offline_report is not None:
        report['community_local_rounds'] = offline_report
    return output, report
