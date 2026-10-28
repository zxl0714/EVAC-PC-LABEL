"""Summarize paired response timestamps, keeping unmeasured endpoints absent.

The SDOJ adapter consumes the existing verified buffer/packet provenance report.
Scene selection is explicit; a valid draw lineage alone does not prove that a
player was alive or that the first changed pixel has been identified.
"""
import argparse
from collections import Counter
import json
import math
from pathlib import Path
import statistics

from project import digest, write_json_new


REQUIRED_POINTS = ('window_event', 'game_button_copy', 'logic_copy',
                   'guest_submit', 'present_begin', 'present_return')
SEGMENTS = (
    ('event_to_game_button_copy', 'window_event', 'game_button_copy'),
    ('game_button_copy_to_logic_copy', 'game_button_copy', 'logic_copy'),
    ('logic_copy_to_guest_submit', 'logic_copy', 'guest_submit'),
    ('guest_submit_to_present_begin', 'guest_submit', 'present_begin'),
    ('present_call', 'present_begin', 'present_return'),
    ('event_to_present_begin', 'window_event', 'present_begin'),
    ('event_to_present_return', 'window_event', 'present_return'),
)


def distribution(values, game_hz):
    ordered = sorted(values)
    if not ordered:
        return {'count': 0, 'median_ms': None, 'p95_ms': None}
    median = statistics.median(ordered) / 1000
    # Small probes remain useful, but their maximum is not a reliable tail.
    enough_for_tail = len(ordered) >= 20
    return dict(count=len(ordered), min_ms=ordered[0] / 1000,
                median_ms=median, median_game_frames=median * game_hz / 1000,
                p95_ms=ordered[math.ceil(len(ordered) * .95) - 1] / 1000
                    if enough_for_tail else None,
                max_ms=ordered[-1] / 1000,
                tail_sample_sufficient=enough_for_tail)


def summarize(samples, game_hz):
    if not isinstance(game_hz, (int, float)) or not math.isfinite(game_hz) or game_hz <= 0:
        raise ValueError('A positive game logical frequency is required')
    seen, accepted, rejected = set(), [], Counter()
    for sample in samples:
        identity = sample['id']
        if identity in seen:
            raise ValueError(f'Duplicate input identity: {identity}')
        seen.add(identity)
        if sample.get('selection') != 'gameplay':
            rejected[sample.get('selection', 'unclassified_scene')] += 1
            continue
        if not sample.get('lineage_verified'):
            rejected['missing_input_output_lineage'] += 1
            continue
        points = sample.get('points_us', {})
        if any(type(points.get(key)) is not int or points[key] < 0 for key in REQUIRED_POINTS):
            rejected['incomplete_or_invalid_timestamps'] += 1
            continue
        if any(points[left] > points[right] for left, right in zip(REQUIRED_POINTS, REQUIRED_POINTS[1:])):
            rejected['clock_or_pairing_order'] += 1
            continue
        intervals = {name: points[end] - points[start] for name, start, end in SEGMENTS}
        # Sum of the five non-overlapping spans must match this SAME response.
        if sum(intervals[name] for name, _, _ in SEGMENTS[:5]) != intervals['event_to_present_return']:
            raise AssertionError('Non-overlapping response decomposition is inconsistent')
        accepted.append(dict(sample, intervals_us=intervals))
    return dict(candidate_responses=len(samples), accepted_responses=len(accepted),
                rejected_responses=dict(rejected), game_hz=game_hz,
                statistics={name: distribution([row['intervals_us'][name] for row in accepted], game_hz)
                            for name, _, _ in SEGMENTS},
                responses=accepted,
                unmeasured=['physical_switch_to_window_event',
                            'first_changed_pixel', 'present_to_first_display', 'panel_photons'])


def adapt_sdoj(report, selection):
    chosen = selection['responses']
    if len({row['event_us'] for row in chosen}) != len(chosen):
        raise ValueError('Duplicate scene-selection input timestamp')
    chosen = {row['event_us']: row for row in chosen}
    samples, found = [], set()
    for candidate in report['input_edges']:
        edge, output = candidate['edge'], candidate.get('output')
        press = edge['press']
        timestamp = press['t_us']
        scene = chosen.get(timestamp)
        if scene:
            found.add(timestamp)
        points = dict(window_event=timestamp)
        if edge.get('raw'):
            points['game_button_copy'] = edge['raw']['t_us']
        if edge.get('copied'):
            points['logic_copy'] = edge['copied']['t_us']
        verified = bool(output and edge.get('copied') and
                        output['lineage']['copy']['seq'] == edge['copied']['seq'] and
                        output.get('first_result_output') and
                        output['present'].get('guest_drawn') and output['present'].get('hr') == 0)
        if output:
            points.update(guest_submit=output['submit']['t_us'],
                          present_begin=output['present']['begin_us'],
                          present_return=output['present']['t_us'])
        samples.append(dict(id=f"window-key-{timestamp}", key=press['key'],
                            selection=scene['classification'] if scene else 'unclassified_scene',
                            scene_evidence=scene.get('evidence') if scene else None,
                            input_origin=selection['input_origin'],
                            basis='input_copy_command_buffer_and_swap_packet_lineage',
                            lineage_verified=verified, points_us=points,
                            logic_copy_seq=edge['copied']['seq'] if edge.get('copied') else None,
                            present_frame=output['present']['frame'] if output else None))
    if found != chosen.keys():
        raise ValueError('Scene selection references an input absent from the trace')
    return samples


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdoj-report', type=Path, required=True)
    parser.add_argument('--selection', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = json.loads(args.sdoj_report.read_text(encoding='utf-8'))
    selection = json.loads(args.selection.read_text(encoding='utf-8'))
    # A selected timestamp is meaningful only for this exact source report.
    if selection['report_sha256'] != digest(args.sdoj_report):
        raise ValueError('Scene selection belongs to a different report')
    for source in report['sources']:
        if digest(Path(source['path'])) != source['sha256']:
            raise ValueError(f"Changed source log: {source['path']}")
    result = summarize(adapt_sdoj(report, selection), selection['game_hz'])
    result.update(game=selection['game'], runtime=selection['runtime'],
                  mode=selection['mode'], status='partial_software_chain',
                  scope='Window key handler to corresponding Present; not physical input or first changed pixel',
                  sources=[dict(path=str(path), sha256=digest(path))
                           for path in (args.sdoj_report, args.selection)])
    write_json_new(args.output, result)
    print(json.dumps({key: value for key, value in result.items() if key != 'responses'}, indent=2))


if __name__ == '__main__':
    main()
