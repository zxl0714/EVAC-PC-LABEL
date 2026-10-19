"""Authored PSB/MDF/script inputs; no extracted game resources or archive seed."""
import hashlib
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts/ketsui'))
import game_fixes as fixes
from ketsui.test_game_fixes import GameFixTests
from daioujou.resource_fixes import fix_replay_peer


def array(values):
    width = max(1, (max(values, default=0).bit_length() + 7) // 8)
    count_width = max(1, (len(values).bit_length() + 7) // 8)
    return (bytes([12 + count_width]) + len(values).to_bytes(count_width, 'little') +
            bytes([12 + width]) + b''.join(value.to_bytes(width, 'little') for value in values))


def psb(root):
    names, strings = [], []
    def collect(value):
        if isinstance(value, dict):
            for key, child in value.items():
                if key not in names: names.append(key)
                collect(child)
        elif isinstance(value, list):
            for child in value: collect(child)
        elif isinstance(value, str) and value not in strings: strings.append(value)
    collect(root)
    charset, parents, terminals = [1], [0], []
    nodes = {b'': 0}
    next_base = 257
    def ensure(node):
        if node >= len(parents): parents.extend([0] * (node + 1 - len(parents)))
        if node >= len(charset): charset.extend([0] * (node + 1 - len(charset)))
    for name in names:
        prefix, parent = b'', 0
        for character in name.encode():
            prefix += bytes([character])
            if prefix not in nodes:
                node = charset[parent] + character
                ensure(node)
                parents[node] = parent
                charset[node] = next_base
                next_base += 256
                nodes[prefix] = node
            parent = nodes[prefix]
        terminal = charset[parent]
        ensure(terminal)
        parents[terminal] = parent
        terminals.append(terminal)
    def integer(value):
        if not value: return b'\4'
        width = next(size for size in range(1, 9) if -(1 << (size * 8 - 1)) <= value < (1 << (size * 8 - 1)))
        return bytes([4 + width]) + value.to_bytes(width, 'little', signed=True)
    def value(node):
        if node is None: return b'\1'
        if isinstance(node, bool): return bytes([3 if node else 2])
        if isinstance(node, int): return integer(node)
        if isinstance(node, float): return b'\x1f' + struct.pack('<d', node)
        if isinstance(node, str): return b'\x18' + struct.pack('<I', strings.index(node))
        if isinstance(node, (list, dict)):
            children = [value(child) for child in (node.values() if isinstance(node, dict) else node)]
            offsets, at = [], 0
            for child in children: offsets.append(at); at += len(child)
            prefix = b'\x21' + array([names.index(key) for key in node]) if isinstance(node, dict) else b'\x20'
            return prefix + array(offsets) + b''.join(children)
        raise ValueError('Unsupported authored PSB value')
    names_raw = array(charset) + array(parents) + array(terminals)
    offsets, text = [], b''
    for item in strings: offsets.append(len(text)); text += item.encode() + b'\0'
    strings_raw = array(offsets)
    header = [0, 40, 40 + len(names_raw), 40 + len(names_raw) + len(strings_raw), 0, 0, 0,
              40 + len(names_raw) + len(strings_raw) + len(text)]
    return b'PSB\0\2\0\0\0' + struct.pack('<8I', *header) + names_raw + strings_raw + text + value(root)


def oracle(item):
    op = item['op']
    if op == 'encode':
        entries = [{**entry, 'replacement': bytes.fromhex(entry['replacement'])} for entry in item['patches']]
        return fixes.encode_read_patches(item['title'], item['version'], entries).hex()
    source = bytes.fromhex(item.get('source', ''))
    if op == 'psb': return fixes.Psb(source).root
    if op == 'fix':
        transform = dict(music=fixes.fix_direct_menu, batch=fixes.fix_ranking_replay_batch,
                         offline=fixes.fix_community_local_round_gate, peer=fix_replay_peer)[item['kind']]
        return transform(source).hex()
    seed, name = bytes.fromhex(item['seed']), item['resource']
    if op == 'mask': return fixes.mask_for(seed, name).hex()
    if op == 'unwrap': return fixes.unwrap(source, seed, name).hex()
    if op == 'wrap': return fixes.wrap_fixed(source, seed, name, item['extent']).hex()
    raise ValueError('Unknown operation')


def change_array(raw, at, index, value):
    count_width = raw[at] - 12
    payload = at + 1 + count_width
    item_width = raw[payload] - 12
    position = payload + 1 + index * item_width
    return raw[:position] + value.to_bytes(item_width, 'little') + raw[position + item_width:]


def cases():
    result = []
    def add(name, op, source=b'', **options):
        result.append(dict(name=name, op=op, source=source.hex(), **options))
    for index, (seed, name) in enumerate(((b'', ''), (b'authored seed', 'own.nut.m'),
                                         (bytes(range(13)), '测试.nut.m'), (bytes(range(255)), 'long-' * 100))):
        add('mask-' + str(index), 'mask', seed=seed.hex(), resource=name)
    seed, name = b'authored seed', 'own.nut.m'
    codec = dict(seed=seed.hex(), resource=name)
    raw = b'authored script\n' * 100
    packed = fixes.wrap_fixed(raw, seed, name, 300)
    for label, body in [('plain', b'not an MDF'), ('valid', packed), ('empty', fixes.wrap_fixed(b'', seed, name, 64)),
                        ('short', b'mdf\0'), ('truncated', packed[:12]),
                        ('wrong-length', packed[:4] + struct.pack('<I', len(raw) - 1) + packed[8:]),
                        ('oversized', packed[:4] + struct.pack('<I', 33 * 1024 * 1024) + packed[8:]),
                        ('trailing', packed[:-1] + bytes([packed[-1] ^ 1]))]:
        add('unwrap-' + label, 'unwrap', body, **codec)
    add('unwrap-wrong-seed', 'unwrap', packed, seed=b'wrong'.hex(), resource=name)
    for label, body, extent in [('normal', raw, 300), ('empty', b'', 64), ('too-small', raw, 8), ('header', raw, 7)]:
        add('wrap-' + label, 'wrap', body, extent=extent, **codec)
    helper = GameFixTests()
    for newline, label in [(b'\n', 'lf'), (b'\r\n', 'crlf')]:
        scripts = dict(music=(b'// remove me' + newline + b'm_bgm_mode[_gamemode] = _bgmMode;' + newline) * 2,
                       batch=helper.authored_ranking_batch(newline), offline=helper.authored_post_game(newline))
        for kind, body in scripts.items():
            add('fix-' + kind + '-' + label, 'fix', body, kind=kind)
            add('fix-' + kind + '-duplicate-' + label, 'fix', body + body, kind=kind)
        add('fix-offline-comments-' + label, 'fix', b'// outside' + newline + scripts['offline'].replace(
            b'    if (checkRegistRanking()) {', b'    // remove' + newline + b'    if (checkRegistRanking()) { // inline'), kind='offline')
        peer = newline.join((b'// outside', b'function is_blocked_id(_id_h,_id_l) {',
            b'  // authored initializer', b'  if (_net.initializeIDMapper() < 0) return true;',
            b'  return authored_block_list(); // preserved', b'}',
            b'function getLocalEntryMax() { return authored_entry_max; }', b''))
        add('fix-peer-' + label, 'fix', peer, kind='peer')
        add('fix-peer-duplicate-' + label, 'fix', peer + peer, kind='peer')
        add('fix-peer-changed-' + label, 'fix', peer.replace(b'< 0', b'<= 0'), kind='peer')
    for count in (0, 1, 3):
        add('fix-music-count-' + str(count), 'fix', b'm_bgm_mode[_gamemode] = _bgmMode;\n' * count, kind='music')
    add('fix-batch-changed', 'fix', helper.authored_ranking_batch().replace(b'            break;', b'            return 0;'), kind='batch')
    add('fix-offline-changed', 'fix', helper.authored_post_game().replace(b'if (checkRegistRanking()) {', b'if (true) {'), kind='offline')
    roots = [None, False, -129, 2**40, -2**48, 'UTF-8 资源', [None, True, 0, -128, 128, 1.5, -2**63, 2**63 - 1],
             {'file_info': {'own.nut.m': [30, 40], 'b': [0, 3]}, 'flags': [True, False], 'text': 'own string'}, {}, []]
    for index, root in enumerate(roots): add('psb-root-' + str(index), 'psb', psb(root))
    valid = psb({'name': 'value'})
    for label, body in [('empty', b''), ('short', valid[:39]), ('signature', b'BAD!' + valid[4:]),
                        ('version', valid[:4] + b'\3' + valid[5:]), ('array-width', valid[:40] + b'\x0c' + valid[41:]),
                        ('truncated-value', valid[:-1]), ('root-offset', valid[:36] + b'\xff' * 4 + valid[40:])]:
        add('psb-invalid-' + label, 'psb', body)
    nested = 0
    for _ in range(66): nested = [nested]
    add('psb-invalid-depth', 'psb', psb(nested))
    own = psb({'a': 0})
    parser = fixes.Psb(own)
    charset, parents_at = parser.array(parser.header[1])
    parents, terminals_at = parser.array(parents_at)
    terminals, _ = parser.array(terminals_at)
    node = parents[terminals[0]]
    add('psb-invalid-name-cycle', 'psb', change_array(own, parents_at, node, node))
    add('psb-invalid-name-terminal', 'psb', change_array(own, terminals_at, 0, 65535))
    add('psb-invalid-name-character', 'psb', change_array(own, parser.header[1], 0, 65535))
    utf8 = psb({'\x7f': 0})
    add('psb-invalid-name-utf8', 'psb', change_array(utf8, 40, 0, 0))
    duplicate = psb({'a': 1, 'b': 2})
    other = fixes.Psb(duplicate)
    add('psb-invalid-duplicate-key', 'psb', change_array(duplicate, other.header[7] + 1, 1, 0))
    _, offsets_at = parser.array(parser.header[7] + 1)
    add('psb-invalid-child-offset', 'psb', change_array(own, offsets_at, 0, 255))
    entry = dict(path='body.bin', file_size=100, offset=30, expected_sha256=hashlib.sha256(b'original').hexdigest(),
                 replacement=b'authored'.hex())
    identity = dict(title='CUSA00001', version='01.02')
    add('encode-valid', 'encode', patches=[entry], **identity)
    add('encode-sorted', 'encode', patches=[{**entry, 'offset': 60}, entry], **identity)
    for index, path in enumerate(('', '../body', '/body', 'a//b', 'a/./b', 'a/../b', 'a\\b', 'C:body', 'a\0b', 'a/')):
        add('encode-path-' + str(index), 'encode', patches=[{**entry, 'path': path}], **identity)
    for label, changes in [('negative', dict(offset=-1)), ('overflow', dict(file_size=1 << 63)),
                           ('range', dict(offset=100)), ('payload-empty', dict(replacement='')), ('hash', dict(expected_sha256='00'))]:
        add('encode-invalid-' + label, 'encode', patches=[{**entry, **changes}], **identity)
    for label, entries in [('empty', []), ('count', [entry] * 17), ('overlap', [entry, {**entry, 'offset': 31}]),
                           ('size', [entry, {**entry, 'offset': 60, 'file_size': 101}])]:
        add('encode-invalid-' + label, 'encode', patches=entries, **identity)
    add('encode-invalid-title', 'encode', patches=[entry], title='CUSA001', version='01.02')
    return result
