"""Memory-only oracle for Feveron's local downloaded-replay viewer."""


def fix_local_replay(source):
    if b'is_blocked_id(' in source or b'initializeIDMapper(' in source:
        raise ValueError('Unexpected Feveron replay identity flow')
    newline = b'\r\n' if b'\r\n' in source else b'\n'
    changes = (
        (b'  function pollCorrectFilter(_key) {', b'  function pollSelectFilter() {',
         b'        if (::ugc_error_dialog())' + newline + b'          return _key;', b''),
        (b'  function pollSelectTab() {', b'  function pollCorrectList(_key) {',
         b'if (!::ugc_error_dialog()) {', b'{'),
    )
    for begin, end, anchor, replacement in changes:
        if source.count(begin) != 1 or source.count(end) != 1:
            raise ValueError('Expected one local-replay method boundary')
        start, stop = source.index(begin), source.index(end)
        if stop <= start:
            raise ValueError('Invalid local-replay method order')
        body = source[start:stop]
        if body.count(anchor) != 1:
            raise ValueError('Local replay differs from the verified control flow')
        body = body.replace(anchor, replacement)
        body = b''.join(line for line in body.splitlines(keepends=True)
                        if not line.lstrip().startswith(b'//'))
        source = source[:start] + body + source[stop:]
    return source
