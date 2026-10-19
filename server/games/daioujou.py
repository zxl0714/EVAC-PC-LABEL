"""DaiOuJou CUSA32997/1.04 native leaderboard profile; not deployed yet."""
import hashlib

from .ketsui import Store as Ps4Store, require


MODE_BASES = {0: 'arcade', 10: 'custom', 20: 'super_easy', 30: 'premium',
              40: 'black_label', 50: 'dodonpachi_iii', 60: 'black_label_one_loop'}
BOARDS = {base + offset: dict(id=base + offset, mode=mode,
                            ship='all' if offset == 6 else ('type_a', 'type_b')[offset % 2],
                            doll='all' if offset == 6 else ('shotia', 'leinyan', 'exy')[offset // 2],
                            upload_limit=0 if mode == 'custom' else 100 if offset == 6 else 10)
          for base, mode in MODE_BASES.items() for offset in range(7)}


def account_id(name):
    raw = hashlib.sha256(b'daioujou-community-v1\0' + name.encode('utf-8')).digest()
    return str((int.from_bytes(raw[:8], 'little') & ((1 << 63) - 1)) or 1)


def ruleset(value, board):
    require(value == 'standard', 'invalid_ruleset')
    return value


class Store(Ps4Store):
    title, version = 'CUSA32997', '1.04'
    api = '/cave/api/v1/games/daioujou'
    application_id = 0x444A5356
    boards, replay_format = BOARDS, 'daioujou-npscore-1.04'
    account_id, ruleset = staticmethod(account_id), staticmethod(ruleset)
    # The native integer omits the final displayed decimal digit (continues).
    max_score = 999999999
    features = ('scores', 'leaderboards')

    def validate_submission(self, board, score):
        require(board // 10 != 1, 'custom_submission_not_supported')

    def validate_replay(self, payload, row):
        # Do not accept Ketsui records or advertise replay support until this
        # title's native envelope and score/character fields are verified.
        require(False, 'native_replay_adapter_not_ready', 501)
