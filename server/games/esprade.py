"""ESP Ra.De. Psi CUSA15155/1.04 native score and replay profile."""
import hashlib

from .ketsui import Store as Ps4Store, require, serve
from ..ps4_replay import network_record


MODE_BASES = {0: 'arcade', 10: 'super_easy', 20: 'premium', 30: 'custom'}
BOARDS = {base + ship: dict(id=base + ship, mode=mode,
                          ship=('all', 'yusuke', 'jb', 'irori', 'alice')[ship],
                          upload_limit=100 if ship == 0 else 10)
          for base, mode in MODE_BASES.items() for ship in range(5)}
BOARDS[88] = dict(id=88, mode='osarai', ship='all', upload_limit=0)
BOARDS[99] = dict(id=99, mode='irori_room', ship='all', upload_limit=0)
REPLAY_VERSIONS = frozenset((0x10019, 0x10020, 0x10021, 0x10022))
REGIONS = tuple(f'ARCADE_{revision:05d}'.encode() for revision in (1000, 1010, 1020, 1030))


def account_id(name):
    raw = hashlib.sha256(b'esprade-community-v1\0' + name.encode('utf-8')).digest()
    return str((int.from_bytes(raw[:8], 'little') & ((1 << 63) - 1)) or 1)


def ruleset(value, board):
    require(value == 'standard', 'invalid_ruleset')
    return value


def replay_info(payload):
    return network_record(payload, versions=REPLAY_VERSIONS, data_versions=(0x10000,),
                          regions=REGIONS, characters=range(8))


class Store(Ps4Store):
    title, version = 'CUSA15155', '1.04'
    api = '/cave/api/v1/games/esprade'
    application_id = 0x45525356
    boards, replay_format = BOARDS, 'esprade-npscore-1.04'
    account_id, ruleset = staticmethod(account_id), staticmethod(ruleset)
    # The original score field is an unsigned 32-bit value. Do not apply
    # DaiOuJou's display-unit conversion or its CUSTOM exclusion here.
    max_score = 0xffffffff

    def validate_replay(self, payload, row):
        info = replay_info(payload)
        group, ship = divmod(row['board'], 10)
        require(group in range(4) and ship <= 4 and info['mode'] == group and
                info['score'] == row['score'] and (ship == 0 or ship == info['character'] % 4 + 1),
                'native_replay_mismatch', 409)


def main():
    serve(Store, 18089)


if __name__ == '__main__':
    main()
