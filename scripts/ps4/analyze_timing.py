"""Bounded-memory frame/audio timeline analysis; no game data or extra dependencies."""
import argparse
from bisect import bisect_left, bisect_right
from collections import Counter
import heapq
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from analysis_paths import private_output
from project import reject_link, write_json_new

HOST_CATEGORIES = ('allocation', 'memory', 'io', 'synchronization', 'sleep', 'fixture')
HOST_ALLOCATIONS = ('unclassified', 'new', 'new_nothrow', 'malloc', 'memalign', 'free_delete',
                    'mspace_create', 'mspace_destroy', 'mspace_memalign', 'mspace_free')


def valid_frame_counters(item):
    fields = ('submission', 'clock_us', 'interval_us', 'counters_began_clock_us', 'thread',
              'active_scopes_at_boundary', 'cross_boundary_completed_scopes', 'outer_calls', 'outer_us')
    if any(type(item.get(field)) is not int or item[field] < 0 for field in fields):
        return False
    if not item['submission'] or not item['thread'] or not item['interval_us']:
        return False
    if type(item.get('same_thread_interval')) is not bool or type(item.get('bounded_to_interval')) is not bool:
        return False
    began, clock = item['counters_began_clock_us'], item['clock_us']
    aligned = began > 0 and clock >= began and clock - began == item['interval_us']
    if 'counters_began_submission' in item:
        previous = item['counters_began_submission']
        if type(previous) is not int or previous < 0:
            return False
        # Rounded absolute clocks can differ from the rounded duration by one
        # us, but only consecutive global submissions define this frame window.
        aligned = (previous == item['submission'] - 1 and began > 0 and clock >= began and
                   0 <= clock - began - item['interval_us'] <= 1)
    if item['same_thread_interval'] != aligned:
        return False
    if item['bounded_to_interval'] and (not item['same_thread_interval'] or item['active_scopes_at_boundary'] or
                                       item['cross_boundary_completed_scopes']):
        return False
    categories = item.get('categories')
    if not isinstance(categories, dict) or set(categories) != set(HOST_CATEGORIES):
        return False
    mode = item.get('allocation_timing', 'full')
    if mode not in ('full', 'counts'):
        return False

    def valid_counter(value, timed):
        if not isinstance(value, dict) or type(value.get('calls')) is not int or value['calls'] < 0:
            return False
        if not timed:
            return 'total_us' in value and 'max_us' in value and value['total_us'] is None and value['max_us'] is None
        return (all(type(value.get(key)) is int and value[key] >= 0 for key in ('total_us', 'max_us')) and
                value['max_us'] <= value['total_us'] and (value['calls'] > 0 or value['total_us'] == 0))

    for category, value in categories.items():
        if not valid_counter(value, category != 'allocation' or mode == 'full'):
            return False
    if item['outer_calls'] > sum(value['calls'] for value in categories.values()) or not item['outer_calls'] and item['outer_us']:
        return False
    if 'allocation_operations' in item:
        operations = item['allocation_operations']
        if not isinstance(operations, dict) or set(operations) != set(HOST_ALLOCATIONS):
            return False
        for value in operations.values():
            if not valid_counter(value, mode == 'full'):
                return False
        if (categories['allocation']['calls'] != min(2**64 - 1, sum(value['calls'] for value in operations.values())) or
            mode == 'full' and categories['allocation']['max_us'] != max(value['max_us'] for value in operations.values())):
            return False
    return True


class Distribution:
    """100 us buckets up to ten seconds, plus an explicit overflow bucket."""
    def __init__(self):
        self.count = self.total = self.maximum = 0
        self.buckets = Counter()

    def add(self, value):
        if type(value) is not int or value < 0:
            raise ValueError('Duration must be a nonnegative integer')
        self.count += 1
        self.total += value
        self.maximum = max(self.maximum, value)
        self.buckets[min(100001, (value + 99) // 100)] += 1

    def report(self):
        def percentile(fraction):
            target = max(1, (self.count * fraction + 99) // 100)
            seen = 0
            for bucket, count in sorted(self.buckets.items()):
                seen += count
                if seen >= target:
                    return bucket * 100 if bucket <= 100000 else None
            return None
        return dict(count=self.count, mean_us=self.total / self.count if self.count else None,
                    median_upper_us=percentile(50), p95_upper_us=percentile(95), p99_upper_us=percentile(99),
                    max_us=self.maximum if self.count else None, bucket_us=100,
                    overflow_samples=self.buckets.get(100001, 0))


def records(path):
    with Path(path).open(encoding='utf-8-sig') as source:
        for number, line in enumerate(source, 1):
            try:
                item = json.loads(line)
                if not isinstance(item, dict) or not isinstance(item.get('event'), str):
                    raise ValueError('Not an event object')
                yield number, item
            except (ValueError, TypeError):
                yield number, None


def analyze(path, threshold_us=25000, top_count=32, correlation_us=100000, *, from_clock_us=None, to_clock_us=None):
    if threshold_us <= 0 or not 1 <= top_count <= 256 or not 0 <= correlation_us <= 1000000:
        raise ValueError('Invalid analysis bounds')
    for bound in (from_clock_us, to_clock_us):
        if bound is not None and (type(bound) is not int or bound < 0):
            raise ValueError('Clock bounds must be nonnegative integers')
    if from_clock_us is not None and to_clock_us is not None and from_clock_us > to_clock_us:
        raise ValueError('Clock window ends before it starts')
    clock_filtered = from_clock_us is not None or to_clock_us is not None
    distributions = {key: Distribution() for key in ('completion_interval_us', 'total_us', 'snapshot_us',
                     'queue_wait_us', 'execute_us', 'present_us', 'between_submissions_us',
                     'diagnostic_log_us', 'previous_frame_log_us')}
    counts = Counter()
    top = []
    compile_timing = {kind: Distribution() for kind in ('shader', 'graphics_pipeline', 'compute_pipeline', 'tiling_pipeline')}
    lock_timing = Distribution()
    top_compiles, top_locks = [], []
    operation_timing = {kind: Distribution() for kind in HOST_CATEGORIES}
    frame_counter_timing = {kind: Distribution() for kind in HOST_CATEGORIES}
    outer_frame_counter_timing = Distribution()
    allocation_frame_counter_timing = {kind: Distribution() for kind in HOST_ALLOCATIONS}
    save_timing = {kind: Distribution() for kind in ('stage', 'commit', 'no_changes')}
    top_operations, top_saves, operation_diagnostics = [], [], []
    markers = []

    def valid_scope(item, duration):
        began, clock = item.get('began_clock_us'), item.get('clock_us')
        return (type(began) is int and type(clock) is int and type(duration) is int and
                0 <= began <= clock and duration == clock - began)

    def remember(heap, duration, number, item):
        candidate = duration, number, item
        if len(heap) < top_count:
            heapq.heappush(heap, candidate)
        elif candidate[:2] > heap[0][:2]:
            heapq.heapreplace(heap, candidate)
    streams, ngs2 = {}, dict(samples=0, timed_samples=0, max_render_us=0, max_graph_wait_us=0,
                            max_registry_wait_us=0, starved_grains_by_system={})
    first_clock = last_clock = None
    regressions = 0
    for number, item in records(path):
        if item is None:
            counts['malformed_lines'] += 1
            continue
        kind = item['event']
        if clock_filtered and kind in {'native_gnm_frame_completed', 'native_audio_timing', 'native_ngs2_render_completed',
                                       'native_gpu_compile_begin', 'native_gpu_compile_completed', 'native_ngs2_rack_lock',
                                       'native_timeline_marker', 'native_host_operation',
                                       'native_save_transaction_staged', 'native_save_transaction_completed', 'native_host_frame_timing'}:
            clock = item.get('completed_clock_us' if kind == 'native_gnm_frame_completed' else 'clock_us')
            if type(clock) is not int or clock < 0:
                counts['unclocked_samples_excluded'] += 1
                continue
            if (from_clock_us is not None and clock < from_clock_us) or (to_clock_us is not None and clock > to_clock_us):
                counts['outside_clock_window_samples'] += 1
                continue
        if kind == 'native_host_frame_timing':
            if not valid_frame_counters(item):
                counts['invalid_host_frame_counters'] += 1
                continue
            counts['host_frame_counter_samples'] += 1
            if item['bounded_to_interval']:
                counts['bounded_host_frame_counter_samples'] += 1
                outer_frame_counter_timing.add(item['outer_us'])
                measured_allocations = item.get('allocation_timing', 'full') == 'full'
                counts['bounded_allocation_timing_' + item.get('allocation_timing', 'full') + '_samples'] += 1
                for category in HOST_CATEGORIES:
                    if category == 'allocation' and not measured_allocations:
                        continue
                    frame_counter_timing[category].add(item['categories'][category]['total_us'])
                if 'allocation_operations' in item and measured_allocations:
                    counts['bounded_allocation_operation_counter_samples'] += 1
                    for operation in HOST_ALLOCATIONS:
                        allocation_frame_counter_timing[operation].add(item['allocation_operations'][operation]['total_us'])
            else:
                counts['unbounded_host_frame_counter_samples'] += 1
        elif kind == 'native_host_timing_diagnostics':
            # This is a whole-process snapshot without a scene timestamp. Keep
            # it explicit, never reinterpret suppression as window-only data.
            if len(operation_diagnostics) < 8:
                operation_diagnostics.append(dict(line=number, event=item))
            else:
                operation_diagnostics[-1] = dict(line=number, event=item)
                counts['host_timing_diagnostic_snapshots_omitted'] += 1
        elif kind == 'native_host_operation':
            duration, category, name = item.get('duration_us'), item.get('category'), item.get('name')
            if (not isinstance(category, str) or category not in operation_timing or
                not isinstance(name, str) or not 1 <= len(name) <= 128 or not valid_scope(item, duration)):
                counts['invalid_host_operation_samples'] += 1
                continue
            counts['host_operation_scopes'] += 1
            operation_timing[category].add(duration)
            remember(top_operations, duration, number, item)
        elif kind in ('native_save_transaction_staged', 'native_save_transaction_completed'):
            category = 'stage' if kind.endswith('_staged') else item.get('action')
            duration = item.get('stage_us' if category == 'stage' else 'total_us')
            if not isinstance(category, str) or category not in save_timing or not valid_scope(item, duration):
                counts['invalid_save_scope_samples'] += 1
                continue
            counts['save_transaction_scopes'] += 1
            save_timing[category].add(duration)
            remember(top_saves, duration, number, item)
        elif kind == 'native_timeline_marker':
            marker_id, clock = item.get('id'), item.get('clock_us')
            if type(marker_id) is not int or marker_id <= 0 or type(clock) is not int or clock < 0 or item.get('source') != 'F8':
                counts['invalid_timeline_markers'] += 1
                continue
            counts['timeline_markers'] += 1
            if len(markers) < 256:
                markers.append(dict(line=number, id=marker_id, clock_us=clock, source='F8'))
            else:
                counts['timeline_markers_omitted'] += 1
        elif kind == 'native_gnm_frame_completed':
            counts['completed_frames'] += 1
            for field, distribution in distributions.items():
                if field == 'between_submissions_us':
                    continue
                value = item.get(field)
                if type(value) is int and value >= 0 and (field != 'completion_interval_us' or value):
                    distribution.add(value)
            interval, duration = item.get('completion_interval_us', 0), item.get('total_us', 0)
            if type(interval) is int and type(duration) is int and interval > 0 and duration >= 0:
                distributions['between_submissions_us'].add(max(0, interval - duration))
            clock = item.get('completed_clock_us')
            if type(clock) is int and clock >= 0:
                if last_clock is not None and clock < last_clock:
                    regressions += 1
                first_clock = clock if first_clock is None else min(first_clock, clock)
                last_clock = clock
            if type(interval) is int and interval >= threshold_us:
                counts['long_frames'] += 1
                candidate = (interval, number, item)
                if len(top) < top_count:
                    heapq.heappush(top, candidate)
                elif candidate[:2] > top[0][:2]:
                    heapq.heapreplace(top, candidate)
        elif kind == 'native_audio_timing':
            counts['audio_timing_samples'] += 1
            key = str(item.get('stream', 'unknown'))
            if key not in streams and len(streams) >= 256:
                counts['additional_audio_streams_omitted'] += 1
                continue
            stream = streams.setdefault(key, dict(samples=0, max_underruns=0, max_submit_gap_us=0,
                                                  max_submit_wait_us=0, max_pump_gap_us=0))
            stream['samples'] += 1
            for field, source in (('max_underruns', 'underruns'), ('max_submit_gap_us', 'max_submit_gap_us'),
                                  ('max_submit_wait_us', 'max_submit_wait_us'), ('max_pump_gap_us', 'max_pump_gap_us')):
                value = item.get(source)
                if type(value) is int and value >= 0:
                    stream[field] = max(stream[field], value)
        elif kind == 'native_ngs2_render_completed':
            ngs2['samples'] += 1
            ngs2['timed_samples'] += type(item.get('clock_us')) is int
            for field in ('render_us', 'graph_wait_us', 'registry_wait_us'):
                value = item.get(field)
                if type(value) is int and value >= 0:
                    ngs2['max_' + field] = max(ngs2['max_' + field], value)
            key = str(item.get('system', 'unknown'))
            value = item.get('starved_grains')
            if type(value) is int and value >= 0 and (key in ngs2['starved_grains_by_system'] or len(ngs2['starved_grains_by_system']) < 256):
                ngs2['starved_grains_by_system'][key] = max(ngs2['starved_grains_by_system'].get(key, 0), value)
        elif kind == 'native_gpu_compile_begin':
            counts['gpu_compile_begins'] += 1
        elif kind == 'native_gpu_compile_completed':
            duration, category = item.get('duration_us'), item.get('kind')
            if not isinstance(category, str) or category not in compile_timing or type(duration) is not int or duration < 0:
                counts['invalid_compile_scope_samples'] += 1
                continue
            counts['gpu_compile_scopes'] += 1
            counts['gpu_compile_unwinding_scopes'] += item.get('unwinding') is True
            compile_timing[category].add(duration)
            remember(top_compiles, duration, number, item)
        elif kind == 'native_ngs2_rack_lock' and item.get('operation') == 'unlock' and item.get('depth') == 0:
            held = item.get('held_us')
            if type(held) is int and held >= 0:
                lock_timing.add(held)
                remember(top_locks, held, number, item)

    selected = [dict(line=line, frame=frame, nearby_events=[], nearby_events_omitted=0)
                for _, line, frame in sorted(top, reverse=True)]
    timed = sorted((entry['frame']['completed_clock_us'], index) for index, entry in enumerate(selected)
                   if type(entry['frame'].get('completed_clock_us')) is int)
    clocks = [clock for clock, _ in timed]
    relevant = {'native_audio_timing', 'native_ngs2_render_completed', 'native_ngs2_atrac9_decoded',
                'native_gpu_compile_begin', 'native_gpu_compile_completed', 'native_ngs2_rack_lock', 'native_timeline_marker',
                'native_host_operation', 'native_save_transaction_staged', 'native_save_transaction_completed',
                'native_host_frame_timing'}

    def keep_context(entry, candidate):
        def priority(value):
            return (value['clock_kind'] == 'scope_overlap',
                    value['event']['event'] in {'native_host_operation', 'native_save_transaction_staged', 'native_save_transaction_completed'},
                    -abs(value['delta_us']))
        retained = entry['nearby_events']
        if len(retained) < 24:
            retained.append(candidate)
        else:
            entry['nearby_events_omitted'] += 1
            worst = min(range(len(retained)), key=lambda index: priority(retained[index]))
            if priority(candidate) > priority(retained[worst]):
                retained[worst] = candidate
    if clocks:
        for number, item in records(path):
            if item is None or item['event'] not in relevant:
                continue
            clock = item.get('clock_us')
            if type(clock) is not int or clock < 0:
                continue
            if item['event'] == 'native_host_frame_timing':
                if valid_frame_counters(item):
                    for index in range(bisect_left(clocks, clock), bisect_right(clocks, clock)):
                        entry = selected[timed[index][1]]
                        frame = entry['frame']
                        if (item['submission'] == frame.get('submission') and item['thread'] == frame.get('host_thread_id') and
                            item['interval_us'] == frame.get('completion_interval_us')):
                            if 'host_frame_counters' in entry:
                                counts['duplicate_host_frame_counters'] += 1
                            else:
                                entry['host_frame_counters'] = dict(line=number, event=item)
                continue
            scoped = (item['event'] == 'native_gpu_compile_completed' or
                      (item['event'] == 'native_ngs2_rack_lock' and item.get('operation') == 'unlock' and item.get('depth') == 0))
            if item['event'] == 'native_host_operation':
                if not valid_scope(item, item.get('duration_us')):
                    continue
                scoped = True
            elif item['event'] in ('native_save_transaction_staged', 'native_save_transaction_completed'):
                if not valid_scope(item, item.get('stage_us' if item['event'].endswith('_staged') else 'total_us')):
                    continue
                scoped = True
            # Underruns are worker observations, delivered by a later producer summary.
            points = [('sample', clock)]
            underrun = item.get('last_underrun_clock_us', 0)
            if type(underrun) is int and underrun > 0:
                points.append(('last_underrun', underrun))
            for clock_kind, point in points:
                for index in range(bisect_left(clocks, point - correlation_us), bisect_right(clocks, point + correlation_us)):
                    entry = selected[timed[index][1]]
                    keep_context(entry, dict(line=number, clock_kind=clock_kind,
                        delta_us=point - timed[index][0], event=item))
            # Compare complete intervals, not only frame-completion points: an
            # operation can begin AND end inside a long frame. Retain scopes
            # from outside a filtered window without adding them to its stats.
            began = item.get('began_clock_us')
            if scoped and type(began) is int and 0 <= began <= clock:
                for frame_clock, selected_index in timed:
                    entry = selected[selected_index]
                    interval = entry['frame'].get('completion_interval_us')
                    start = max(0, frame_clock - interval) if type(interval) is int and interval >= 0 else frame_clock
                    overlap = min(clock, frame_clock) - max(began, start)
                    if overlap < 0 or abs(clock - frame_clock) <= correlation_us:
                        continue
                    keep_context(entry, dict(line=number, clock_kind='scope_overlap',
                        delta_us=clock - frame_clock, overlap_us=overlap, event=item))

    for entry in selected:
        entry['nearby_events'].sort(key=lambda value: (value['line'], value['clock_kind']))

    return dict(format=1, input=str(Path(path).resolve()), threshold_us=threshold_us,
                clock_filter=dict(from_clock_us=from_clock_us, to_clock_us=to_clock_us,
                    inclusive=True, enabled=clock_filtered),
                counts=dict(counts), timeline_markers=markers, frame_timing={key: value.report() for key, value in distributions.items()},
                frame_clock_span_us=last_clock - first_clock if first_clock is not None and last_clock is not None else None,
                frame_clock_regressions=regressions, audio_streams=streams, ngs2=ngs2, longest_frames=selected,
                gpu_compilation={kind: distribution.report() for kind, distribution in compile_timing.items()},
                longest_compiles=[dict(line=line, event=event) for _, line, event in sorted(top_compiles, reverse=True)],
                outer_rack_lock_holds=lock_timing.report(),
                longest_rack_lock_holds=[dict(line=line, event=event) for _, line, event in sorted(top_locks, reverse=True)],
                host_operations={kind: distribution.report() for kind, distribution in operation_timing.items()},
                longest_host_operations=[dict(line=line, event=event) for _, line, event in sorted(top_operations, reverse=True)],
                host_operation_diagnostics=operation_diagnostics,
                host_frame_counters=dict(outer_us=outer_frame_counter_timing.report(),
                    category_inclusive_us={kind: value.report() for kind, value in frame_counter_timing.items()},
                    allocation_operation_inclusive_us={kind: value.report() for kind, value in allocation_frame_counter_timing.items()},
                    measurement='Completed-frame diagnostic samples only; distributions include bounded same-thread intervals, not every frame. Categories are inclusive; outer_us includes timed scopes only and avoids nested double counting. Counts-only allocation durations are null/unmeasured, not zero, and excluded from duration distributions.'),
                save_transactions={kind: distribution.report() for kind, distribution in save_timing.items()},
                longest_save_operations=[dict(line=line, event=event) for _, line, event in sorted(top_saves, reverse=True)],
                measurement='Host steady-clock timings; CPU submission and Present completion, not GPU timestamps or physical scanout/speaker time.',
                interpretation=['between_submissions_us includes guest work, scheduling and diagnostics outside SubmitAndFlip.',
                    'diagnostic_log_us measures three submit/journal/execution printf sites, not all logging; previous_frame_log_us belongs to the preceding frame record and the current completion interval.',
                    'F8 markers are human observations, not automatic phase or hitch detection; at most 256 are listed.',
                    'Underruns can include intentional silence or idle; cumulative maxima do not locate a particular hitch.',
                    'AudioOut cumulative counters/maxima retain stream-lifetime meaning even when samples are clock-filtered.',
                    'Nearby context may include summaries outside the selected window, including delayed underrun reports.',
                    'Compile/lock durations are whole scopes, attributed by completion time when clock-filtered; they are not clipped to the window.',
                    'Host-operation and save durations are whole scopes, attributed by completion time; save stage and unmount commit are separate, not an entire mount transaction.',
                    'Host-operation records are thresholded/rate-limited wall times, including scheduling and diagnostics, not pure CPU work; intentional sleep/waits are not automatically defects.',
                    'Host-operation diagnostic snapshots describe the whole process, not the clock window. Suppressed or absent records cannot exclude a slow operation.',
                    'Frame counters match completion clock, submission, thread and interval exactly. Unaligned/cross-boundary scopes remain visible but are excluded from bounded statistics; category times are inclusive and are not summed as exclusive CPU costs.',
                    'Explicit rack holds can include intentional guest pauses or resets; a long hold alone does not justify removing synchronization.',
                    'Nearby events establish timing coincidence, not causation or same-thread blocking. Scope intersection uses the full completion interval; at most 24 records are retained with omitted counts and overlap/host/save priority.',
                    'Older logs without audio clock_us cannot establish frame/audio correlation.',
                    'Malformed or live partial JSON lines are counted and excluded; sample cadence does not cover every audio grain.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path, help='stdout.jsonl or its run directory')
    parser.add_argument('--threshold-us', type=int, default=25000)
    parser.add_argument('--top', type=int, default=32)
    parser.add_argument('--from-clock-us', type=int, help='Inclusive host monotonic clock start, not wall-clock time')
    parser.add_argument('--to-clock-us', type=int, help='Inclusive host monotonic clock end')
    parser.add_argument('--output', type=Path, help='New output file; existing reports are not overwritten')
    args = parser.parse_args()
    path = args.log / 'stdout.jsonl' if args.log.is_dir() else args.log
    reject_link(path)
    output = private_output(args.output, path) if args.output else None
    result = analyze(path, args.threshold_us, args.top, from_clock_us=args.from_clock_us, to_clock_us=args.to_clock_us)
    if output:
        write_json_new(output, result)
    else:
        print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
