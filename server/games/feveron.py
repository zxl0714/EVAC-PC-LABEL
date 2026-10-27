"""Dangun Feveron CUSA06735/1.00 native score and replay profile."""
import hashlib

from .ketsui import Store as Ps4Store, require, serve
from ..ps4_replay import network_record


MODE_BASES = {0: 'arcade', 10: 'arcade_time_attack',
              20: 'super_easy', 30: 'super_easy_time_attack',
              40: 'fever', 50: 'fever_time_attack',
              60: 'custom', 70: 'custom_time_attack'}
BOARDS = {base + ship: dict(id=base + ship, mode=mode,
                          ship=('all', 'type_a', 'type_b', 'type_c', 'uotaro_poko')[ship],
                          upload_limit=100 if ship == 0 and base < 60 else 10)
          for base, mode in MODE_BASES.items() for ship in range(5)}
REPLAY_VERSIONS = frozenset((0x10001, 0x10002))
REGIONS = (b'ARCADE_01000', b'ARCADE_01010')
# Native character metadata packs side/ship, weapon and speed, not a ship index.
CHARACTERS = frozenset((ship << 8) | (weapon << 4) | speed
                      for ship in range(8) for weapon in range(4) for speed in range(5))


def account_id(name):
    raw = hashlib.sha256(b'feveron-community-v1\0' + name.encode('utf-8')).digest()
    return str((int.from_bytes(raw[:8], 'little') & ((1 << 63) - 1)) or 1)


def ruleset(value, board):
    require(value == 'standard', 'invalid_ruleset')
    return value


def replay_info(payload):
    return network_record(payload, versions=REPLAY_VERSIONS, data_versions=(0x10000,),
                          regions=REGIONS, characters=CHARACTERS)


class Store(Ps4Store):
    title, version = 'CUSA06735', '1.00'
    api = '/cave/api/v1/games/feveron'
    application_id = 0x46455356
    boards, replay_format = BOARDS, 'feveron-npscore-1.00'
    account_id, ruleset = staticmethod(account_id), staticmethod(ruleset)
    max_score = 0xffffffff

    def validate_replay(self, payload, row):
        info = replay_info(payload)
        group, ship = divmod(row['board'], 10)
        # Time Attack is a distinct native board, not another GameMode value
        # in the network record. Do not rewrite original snapshots or fields.
        require(group in range(8) and ship <= 4 and info['mode'] == group // 2 and
                info['score'] == row['score'] and
                (ship == 0 or ship == ((info['character'] >> 8) & 3) + 1),
                'native_replay_mismatch', 409)


def main():
    serve(Store, 18091)


if __name__ == '__main__':
    main()
