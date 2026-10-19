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


def fix_local_replay(source):
    """Keep community-cache viewing local; do not change global NP state."""
    newline = b'\r\n' if b'\r\n' in source else b'\n'
    def method(raw, begin, end, anchor, replacement):
        if raw.count(begin) != 1 or raw.count(end) != 1:
            raise ValueError('Expected one local-replay method boundary')
        start, stop = raw.index(begin), raw.index(end)
        if stop <= start:
            raise ValueError('Invalid local-replay method order')
        body = raw[start:stop]
        if body.count(anchor) != 1:
            raise ValueError('Local replay differs from the verified control flow')
        body = body.replace(anchor, replacement)
        body = b''.join(line for line in body.splitlines(keepends=True)
                        if not line.lstrip().startswith(b'//'))
        return raw[:start] + body + raw[stop:]
    changes = (
        (b'  function pollCorrectFilter(_key) {', b'  function pollSelectFilter() {',
         b'        if (System.getSpec() != "nx" && ::ugc_error_dialog())' + newline + b'          return _key;', b''),
        (b'  function pollSelectTab() {', b'  function is_blocked_id(_id_h,_id_l) {',
         b'if (!::ugc_error_dialog()) {', b'{'),
        (b'  function pollCorrectList(_key) {', b'  function pollCancel() {',
         b'          if (System.getSpec() == "ps4" && m_tab_select == REPLAY_TAB_DOWN &&' + newline +
         b'            is_blocked_id(m_replay_array[_n]._01_added_value._18_account_h, m_replay_array[_n]._01_added_value._19_account_l)) _ok = false;', b''),
        (b'  function setReplayPage(_start, _cnt, _array, _list) {', b'  function getReplaySlot(_array) {',
         newline.join((b'    if (System.getSpec() == "ps4" && m_tab_select == REPLAY_TAB_DOWN) {',
             b'      ::g_menu_nowloading.open(NowLoadingTypes.NOW_ACCESSING);',
             b'      _net = NetworkModule(null);',
             b'      if (_net != null && _net.initializeIDMapper() >= 0) accountID = true;', b'    }')), b''),
    )
    for change in changes:
        source = method(source, *change)
    return source
