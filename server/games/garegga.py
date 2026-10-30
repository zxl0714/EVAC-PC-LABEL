"""Battle Garegga CUSA10207/1.00 native score and replay profile."""
import hashlib

from .ketsui import Store as Ps4Store, require, serve
from ..ps4_replay import network_record

MODES = ('arcade', 'super_easy', 'premium', 'custom')
SHIPS = ('all', 'silver_sword', 'grass_hopper', 'flying_baron', 'wild_snail',
         'gain', 'chitta', 'miyamoto', 'bornnam')
BOARDS = {mode * 10 + ship: dict(id=mode * 10 + ship, mode=name, ship=SHIPS[ship],
                               upload_limit=100 if ship == 0 else 10)
          for mode, name in enumerate(MODES) for ship in range(9)}
REPLAY_VERSIONS = frozenset((0x10012, 0x10100, 0x10200, 0x10300, 0x10400))
REGIONS = tuple(f'ARCADE_{version:05d}'.encode() for version in (1000, 1010, 1020, 1030, 1040))


def account_id(name):
    raw = hashlib.sha256(b'garegga-community-v1\0' + name.encode('utf-8')).digest()
    return str((int.from_bytes(raw[:8], 'little') & ((1 << 63) - 1)) or 1)


def ruleset(value, board):
    require(value == 'standard', 'invalid_ruleset')
    return value


def replay_info(payload):
    return network_record(payload, versions=REPLAY_VERSIONS, data_versions=(0x10000,),
                          regions=REGIONS, characters=range(128))


class Store(Ps4Store):
    title, version = 'CUSA10207', '1.00'
    api = '/cave/api/v1/games/garegga'
    application_id = 0x47415356
    boards, replay_format = BOARDS, 'garegga-npscore-1.00'
    account_id, ruleset = staticmethod(account_id), staticmethod(ruleset)
    max_score = 0xffffffff

    def validate_replay(self, payload, row):
        info = replay_info(payload)
        mode, ship = divmod(row['board'], 10)
        require(info['mode'] == mode and info['score'] == row['score'] and
                (ship == 0 or ship == ((info['character'] >> 4) & 7) + 1),
                'native_replay_mismatch', 409)


def main():
    serve(Store, 18092)


if __name__ == '__main__':
    main()
