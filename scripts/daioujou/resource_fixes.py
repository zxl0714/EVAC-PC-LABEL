"""Checked community-only title adapter; reads no resource or archive seed."""


def fix_replay_peer(source):
    begin = b'function is_blocked_id(_id_h,_id_l) {'
    end = b'function getLocalEntryMax() {'
    anchor = b'  if (_net.initializeIDMapper() < 0) return true;'
    if source.count(begin) != 1 or source.count(end) != 1:
        raise ValueError('Expected one peer-check method boundary')
    start, stop = source.index(begin), source.index(end)
    if stop <= start:
        raise ValueError('Invalid peer-check method order')
    body = source[start:stop]
    if body.count(anchor) != 1:
        raise ValueError('Peer initializer differs from the verified control flow')
    newline = b'\r\n' if b'\r\n' in source else b'\n'
    replacement = newline.join((b'  if (_net.initializeIDMapper() < 0) {',
        b'    if (::g_ranking_manager && ::g_ranking_manager.isInitialized()) return false;',
        b'    return true;', b'  }'))
    body = body.replace(anchor, replacement)
    body = b''.join(line for line in body.splitlines(keepends=True)
                    if not line.lstrip().startswith(b'//'))
    return source[:start] + body + source[stop:]
