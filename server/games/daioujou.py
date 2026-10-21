"""Independent DaiOuJou CUSA32997/1.04 native leaderboard profile."""
import hashlib
import struct
import zlib

from .ketsui import Store as Ps4Store, require, ApiError, MAX_REPLAY, serve


MODE_BASES = {0: 'arcade', 10: 'custom', 20: 'super_easy', 30: 'premium',
              40: 'black_label', 50: 'dodonpachi_iii', 60: 'black_label_one_loop'}
BOARDS = {base + offset: dict(id=base + offset, mode=mode,
                            ship='all' if offset == 6 else ('type_a', 'type_b')[offset % 2],
                            doll='all' if offset == 6 else ('shotia', 'leinyan', 'exy')[offset // 2],
                            upload_limit=0 if mode == 'custom' else 100 if offset == 6 else 10)
          for base, mode in MODE_BASES.items() for offset in range(7)}
# AC-CHALLENGE ranks cumulative medals, not a round/replay. Native 1.04 uses
# 66 scenes x two ships x three dolls, with at most 10,000 points per scene.
CHALLENGE_BOARD, MAX_CHALLENGE_POINTS = 99, 66 * 2 * 3 * 10000
BOARDS[CHALLENGE_BOARD] = dict(id=CHALLENGE_BOARD, mode='ac_challenge', ship='all', doll='all', upload_limit=0)


def account_id(name):
    raw = hashlib.sha256(b'daioujou-community-v1\0' + name.encode('utf-8')).digest()
    return str((int.from_bytes(raw[:8], 'little') & ((1 << 63) - 1)) or 1)


def ruleset(value, board):
    require(value == 'standard', 'invalid_ruleset')
    return value


def _inflate(payload, limit):
    require(0 < len(payload) <= MAX_REPLAY, 'invalid_replay_size')
    decoder=zlib.decompressobj()
    try:
        plain=decoder.decompress(payload,limit+1)
    except zlib.error:
        raise ApiError(400,'invalid_replay_compression') from None
    require(len(plain)<=limit and decoder.eof and not decoder.unused_data and not decoder.unconsumed_tail,
            'invalid_replay_compression')
    return plain


def replay_info(payload):
    # RankingEntry copies named fields into system/config/struct_replay.psb,
    # not the smaller 0x65600-byte record used by the title's local save.
    raw=_inflate(payload,0x80000)
    require(len(raw)==0x80000,'invalid_replay_size')
    version,core,extent,screen,total,consumed,snapshot,players,mask,pages=struct.unpack_from('<10I',raw)
    require(version==0x10030 and core==0x10000 and extent<=0x78000 and screen==0,'invalid_replay_version')
    require(total and not total%512 and 0<consumed<=total and 0<snapshot<=0x78000 and
            1<=players<=8 and mask,'invalid_replay_header')
    stride=total//512
    lines=players*mask.bit_count()
    directory_bytes=stride*lines
    remaining=0x78000-snapshot
    require(directory_bytes<remaining and pages<(remaining-directory_bytes)//8,'invalid_replay_keys')
    directory=raw[0x500+snapshot:0x500+snapshot+directory_bytes]
    require(sum(value.bit_count() for value in directory)==pages,'invalid_replay_keys')
    occupied=[]
    for line in range(lines):
        for index,value in enumerate(directory[line*stride:(line+1)*stride]):
            while value:
                bit=(value & -value).bit_length()-1
                occupied.append((index*8+bit,line))
                value &= value-1
    words=0x500+snapshot+directory_bytes
    for index,(page,line) in enumerate(sorted(occupied)):
        require(page*64<consumed,'invalid_replay_keys')
        word=struct.unpack_from('<Q',raw,words+index*8)[0]
        frames=min(64,consumed-page*64)
        require(word and (frames==64 or not word>>frames),'invalid_replay_keys')
    state=_inflate(raw[0x500:0x500+snapshot],2*1024*1024)
    cursor=0
    names=set()
    while cursor<len(state):
        length=state[cursor];cursor+=1
        require(length and len(state)-cursor>=length+4 and len(names)<4096,'invalid_replay_snapshot')
        name=state[cursor:cursor+length];cursor+=length
        require(all(0x20<=c<=0x7e for c in name) and name not in names,'invalid_replay_snapshot')
        names.add(name)
        size=struct.unpack_from('<I',state,cursor)[0];cursor+=4
        require(size<=len(state)-cursor,'invalid_replay_snapshot')
        cursor+=size
    require(names,'invalid_replay_snapshot')
    def text(offset,size):
        value=raw[offset:offset+size]
        require(0 in value,'invalid_replay_text')
        return value.split(b'\0',1)[0]
    require(text(48,16),'invalid_replay_title')
    region=text(80,16)
    require(region in (b'ARCADE_01000',b'ARCADE_01010',b'ARCADE_01020',b'ARCADE_01030'),'invalid_replay_core')
    text(120,17);text(137,17)
    kind,mode,score,character,stage=struct.unpack_from('<HHIHH',raw,104)
    require(kind==0 and character<6,'invalid_replay_metadata')
    return dict(score=score,character=character,mode=mode,stage=stage,region=region.decode(),
                version=version,data_version=core,consumed_frames=consumed,record_sha256=hashlib.sha256(raw).hexdigest())


class Store(Ps4Store):
    title, version = 'CUSA32997', '1.04'
    api = '/cave/api/v1/games/daioujou'
    application_id = 0x444A5356
    boards, replay_format = BOARDS, 'daioujou-npscore-1.04'
    account_id, ruleset = staticmethod(account_id), staticmethod(ruleset)
    # The native integer omits the final displayed decimal digit (continues).
    max_score = 999999999

    def validate_submission(self, board, score):
        require(board // 10 != 1, 'custom_submission_not_supported')
        require(board != CHALLENGE_BOARD or score <= MAX_CHALLENGE_POINTS, 'challenge_points_out_of_range')

    def validate_replay(self, payload, row):
        info=replay_info(payload)
        group,character=divmod(row['board'],10)
        # The native producer can temporarily use CUSTOM=3 for eligible
        # ARCADE settings, then rank it as ARCADE via rankingModeConv. Its
        # network header retains the raw 3; the local save later stores 0.
        # CUSTOM board group 1 remains excluded; this is not an anti-cheat test.
        modes={0:(0,3),2:(1,),3:(2,),4:(4,),5:(5,),6:(4,)}
        require(group in modes and character<=6 and info['mode'] in modes[group] and
                info['score']==row['score'] and (character==6 or character==info['character']),
                'native_replay_mismatch',409)


def main():
    serve(Store,18088)


if __name__=='__main__':
    main()
