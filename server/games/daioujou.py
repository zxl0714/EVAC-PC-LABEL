"""DaiOuJou CUSA32997 native leaderboards for the 1.00 and 1.04 releases."""
import hashlib
from .ketsui import Store as Ps4Store, require, serve
from ..ps4_replay import network_record


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


def replay_info(payload):
    # Same framing, independent native title/version and character contract.
    info = network_record(payload, versions=(0x1000e, 0x10030), data_versions=(0x10000,),
                          regions=tuple(f'ARCADE_{revision:05d}'.encode() for revision in (1000, 1010, 1020, 1030)),
                          characters=range(6))
    require(info['version'] != 0x1000e or info['region'] == 'ARCADE_01000', 'invalid_replay_core')
    return info


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
