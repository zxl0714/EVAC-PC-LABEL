"""Inventory native community boundaries from a verified local census (no execution)."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from project import ROOT, digest, write_json_new
from analysis_paths import analysis_child, private_output


TABLE_ADDRESS = 0x820066B8
# Manually reviewed function boundaries; reports retain hashes, never game bodies.
BOUNDARIES = {
    'prepare_native_run': (0x820A7748, 0x820A79F0),
    'build_write_views': (0x820A79F0, 0x820A7D00),
    'read_previous_scores': (0x820A7DA0, 0x820A7EA0),
    'replay_rank_eligibility': (0x820A8088, 0x820A8100),
    'filter_personal_improvements': (0x820A8100, 0x820A8218),
    'write_score_batch': (0x820A8840, 0x820A88E8),
    'native_task_state_machine': (0x820AA770, 0x820AB880),
    'storage_download_boundary': (0x820D8ED8, 0x820D91B8),
    'storage_upload_boundary': (0x820D91B8, 0x820D9448),
    'estimate_rank_boundary': (0x820D9688, 0x820D9864),
    'write_stats_boundary': (0x820D9F20, 0x820D9FE0),
    'local_storage_path_boundary': (0x820D9FE0, 0x820DA078),
    'remote_storage_path_boundary': (0x820DA078, 0x820DA110),
    'read_stats_boundary': (0x820DC5F0, 0x820DC6C8),
    'stats_enumerator_boundary': (0x820DC6C8, 0x820DC728),
}


def view_table(data, offset, limit=256):
    """Return metadata only. Unknown selector meanings stay numeric."""
    if offset < 0 or offset % 4 or not 1 <= limit <= 256:
        raise ValueError('Invalid view table bounds')
    result, seen = [], set()
    for index in range(limit):
        position = offset + index * 24
        if position + 4 > len(data):
            raise ValueError('Unterminated view table')
        view = struct.unpack_from('>I', data, position)[0]
        if view == 0xffffffff:
            if not result:
                raise ValueError('Empty view table')
            return result
        if position + 24 > len(data):
            raise ValueError('Truncated view table')
        view, combined, character, mode, flag, category = struct.unpack_from('>IIiIII', data, position)
        if (not 1 <= view <= 0xffff or view in seen or combined not in (0, 1)
                or not -1 <= character <= 2 or not 20 <= mode <= 26
                or flag not in (0, 1) or category not in (0, 1, 2)):
            raise ValueError('Unsupported native view metadata')
        seen.add(view)
        result.append(dict(view=view, combinedPlayers=combined, character=character,
                           mode=mode, flag=flag, category=category))
    raise ValueError('View table exceeded its bound')


def in_analysis(analysis, relative):
    return analysis_child(analysis, relative)


def inventory(analysis, data_dir):
    analysis = Path(analysis).resolve()
    receipt = json.loads(in_analysis(analysis, 'analysis.json').read_text(encoding='utf-8'))
    supported = next(item for item in json.loads((ROOT / 'games/espgaluda2/inputs.json').read_text(
        encoding='utf-8'))['xex'] if item['path'] == 'default.xex')
    item = next(item for item in receipt['modules'] if item['module'] == 'default.xex')
    if (receipt.get('guestExecution') is not False or receipt.get('game') != 'espgaluda2'
            or item['inputSha256'] != supported['sha256']
            or digest(analysis_child(data_dir, 'default.xex')) != supported['sha256']):
        raise ValueError('Census must match the supported, unchanged default.xex')
    census = json.loads(in_analysis(analysis, 'default/census.json').read_text(encoding='utf-8'))
    if (census.get('module') != 'default.xex' or census.get('titleId') != int(supported['titleId'], 16)
            or census.get('guestExecution') is not False):
        raise ValueError('Unexpected census title')
    hashes = {entry['path']: entry['sha256'] for entry in item['dumps']}
    sections = []
    for section in census['sections']:
        if section['name'] not in ('.rdata', '.text') or not section['inImage']:
            continue
        relative = 'default/' + section['dump']
        path = in_analysis(analysis, relative)
        if digest(path) != hashes[relative] or path.stat().st_size != section['bytes']:
            raise ValueError('Census section differs from its hash receipt')
        sections.append((section, path.read_bytes()))

    def read(address, size):
        for section, data in sections:
            offset = address - section['address']
            if 0 <= offset and offset + size <= len(data):
                return data[offset:offset + size]
        raise ValueError('Reviewed address outside verified sections')

    rdata, data = next((section, data) for section, data in sections if section['name'] == '.rdata')
    views = view_table(data, TABLE_ADDRESS - rdata['address'])
    # Validate the reviewed Japanese confirmation as an additional anchor.
    # The report records its meaning, not the copyrighted resource bytes.
    prompt = data[0x82006F40 - rdata['address']:].split(b'\0', 1)[0].decode('cp932')
    if not prompt.startswith('スコア登録後にリプレイデータの'):
        raise ValueError('Native score-before-replay confirmation changed')
    boundaries = [dict(name=name, address=f'0x{start:08X}', bytes=end - start,
                       sha256=hashlib.sha256(read(start, end - start)).hexdigest())
                  for name, (start, end) in BOUNDARIES.items()]
    return dict(format=1, titleId=supported['titleId'], inputSha256=supported['sha256'],
                guestExecution=False, runtimeBehaviorVerified=False,
                semanticReview='games/espgaluda2/docs/PROGRAM_LAYOUT.md#原生社区流程',
                viewTableAddress=f'0x{TABLE_ADDRESS:08X}', views=views, boundaries=boundaries,
                summary=dict(views=len(views), modes=sorted({v['mode'] for v in views}),
                             categories=sorted({v['category'] for v in views}),
                             characters=sorted({v['character'] for v in views})))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('analysis', type=Path)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = private_output(args.output, args.analysis, args.data_dir)
    result = inventory(args.analysis, args.data_dir)
    write_json_new(output, result)
    print(json.dumps(dict(output=str(output), **result['summary'],
                          guestExecution=False, runtimeBehaviorVerified=False), indent=2))


if __name__ == '__main__':
    main()
