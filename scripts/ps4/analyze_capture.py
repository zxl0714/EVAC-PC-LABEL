"""Correlate opt-in PS4 capture events with frame tails and player F8 markers.

Two passes keep memory bounded by the selected incident windows, not session
length. Host wall intervals overlap; GPU timestamp spans are not GPU busy time.
"""
import argparse
from collections import Counter
import hashlib
import heapq
import json
from pathlib import Path


def events(path):
    with path.open(encoding='utf-8', errors='replace') as source:
        for line in source:
            try:
                item = json.loads(line)
            except ValueError:
                continue
            if isinstance(item, dict):
                yield item


def derive(item):
    result = dict(item)
    if item.get('event') == 'native_gpu_worker_handoff':
        names = ('began_clock_us', 'enqueued_clock_us', 'started_clock_us',
                 'finished_clock_us', 'clock_us')
        times = [item[name] for name in names]
        result['ordered'] = times == sorted(times)
        result['parts_us'] = dict(zip(('enqueue', 'queued', 'operation', 'caller_resume'),
                                     (b-a for a, b in zip(times, times[1:]))))
        # Thread CPU accounting is coarse and may return zero for short work.
        result['worker_cpu_us'] = item['worker_cpu_100ns']/10
    elif item.get('event') == 'native_pacing_reserve':
        result['target_in_us'] = (item['target_qpc']-item['qpc'])*1e6/item['qpc_frequency']
    elif item.get('event') == 'native_pacing_wait':
        result['mutex_wait_us'] = item['locked_clock_us']-item['began_clock_us']
        result['resume_after_notification_us'] = max(0, item['clock_us']-max(
            item['notified_clock_us'], item['locked_clock_us']))
        result['extra_boundaries'] = item['completed_counter']-item['target_counter']
    elif item.get('event') in ('native_gpu_snapshot_handoff', 'native_gpu_window_present'):
        if item['event'] == 'native_gpu_snapshot_handoff':
            names = ('began_clock_us', 'submitted_clock_us', 'paced_clock_us',
                     'copied_clock_us', 'clock_us')
            parts = ('prepare_and_submit', 'pacing', 'copy_fence', 'publish')
        else:
            names = ('began_clock_us', 'retired_clock_us', 'acquired_clock_us',
                     'recorded_clock_us', 'submitted_clock_us', 'queued_clock_us', 'clock_us')
            parts = ('setup_and_retire', 'acquire', 'record', 'submit', 'queue_present', 'composite_fence')
        times = [item[name] for name in names]
        result['ordered'] = times == sorted(times)
        result['parts_us'] = dict(zip(parts, (b-a for a, b in zip(times, times[1:]))))
    return result


class TimingStream:
    """Constant-memory host timing summary; no inference of physical display drops."""
    def __init__(self, clock):
        self.clock = clock
        self.previous = None
        self.records = self.resets = self.sequence_gaps = self.invalid = 0
        self.metrics = {}

    def add_metric(self, name, value):
        metric = self.metrics.setdefault(name, dict(count=0, total_us=0, max_us=0,
                                                    over18ms=0, over20ms=0, over30ms=0))
        metric['count'] += 1
        metric['total_us'] += value
        metric['max_us'] = max(metric['max_us'], value)
        for ms in (18, 20, 30):
            metric[f'over{ms}ms'] += value > ms*1000

    def add(self, item):
        self.records += 1
        derived = derive(item)
        if not derived['ordered']:
            self.invalid += 1
            self.previous = None
            return None
        for name, value in derived['parts_us'].items():
            self.add_metric(name, value)
        interval = None
        if self.previous is not None:
            previous = self.previous
            if (item['sequence'] <= previous['sequence'] or item[self.clock] < previous[self.clock]
                    or item['thread'] != previous['thread']):
                self.resets += 1
            else:
                self.sequence_gaps += item['sequence'] != previous['sequence']+1
                interval = item[self.clock]-previous[self.clock]
                self.add_metric('interval', interval)
                if item['sequence'] == previous['sequence']+1:
                    self.add_metric('consecutive_interval', interval)
        self.previous = item
        return interval

    def report(self):
        return dict(clock=self.clock, records=self.records, resets=self.resets,
                    sequenceGaps=self.sequence_gaps, invalidOrder=self.invalid,
                    metrics={name: dict(value, mean_us=value['total_us']/value['count'])
                             for name, value in self.metrics.items()})


def analyze(path, top=12):
    tails, markers, counts = [], [], Counter()
    present_tails = []
    streams = {'native_gpu_snapshot_handoff': TimingStream('clock_us'),
               'native_gpu_window_present': TimingStream('queued_clock_us')}
    capture, logger, timestamp_support = None, None, None
    frames = over18 = over20 = over30 = 0
    for line, item in enumerate(events(path)):
        event = item.get('event', '')
        counts[event] += 1
        if event in streams:
            interval = streams[event].add(item)
            if event == 'native_gpu_window_present' and interval is not None:
                heapq.heappush(present_tails, (interval, line, item))
                if len(present_tails) > top:
                    heapq.heappop(present_tails)
        if event == 'native_gnm_frame_completed':
            frames += 1
            interval = item.get('completion_interval_us', 0)
            over18 += interval > 18000
            over20 += interval > 20000
            over30 += interval > 30000
            heapq.heappush(tails, (interval, line, item))
            if len(tails) > top:
                heapq.heappop(tails)
        elif event == 'native_timeline_marker':
            markers.append(item)
            markers = markers[-32:]
        elif event == 'native_timing_capture':
            capture = item
        elif event == 'native_async_log':
            logger = item
        elif event == 'native_gpu_timestamp_support':
            timestamp_support = item
    windows = []
    for _, _, frame in sorted(tails, reverse=True):
        end = frame['completed_clock_us']
        windows.append(dict(kind='long_frame', frame=frame, begin=end-100000, end=end+50000, events=[]))
    for marker in markers:
        end = marker['clock_us']
        windows.append(dict(kind='player_marker', marker=marker, begin=end-5000000, end=end+1000000, events=[]))
    for interval, _, present in sorted(present_tails, reverse=True):
        end = present['queued_clock_us']
        windows.append(dict(kind='window_queue_tail', present=derive(present), interval_us=interval,
                            begin=end-100000, end=end+50000, events=[]))
    for item in events(path):
        event = item.get('event', '')
        if not (event.startswith(('native_gpu_', 'native_pacing_', 'native_host_operation', 'native_host_wait_detail',
                                 'native_audio_', 'native_ngs_', 'native_save_', 'community_'))
                or event in ('native_gnm_frame_completed', 'native_async_log')):
            continue
        end = item.get('clock_us', item.get('completed_clock_us'))
        if not isinstance(end, (int, float)):
            continue
        for window in windows:
            if window['begin'] <= end <= window['end']:
                if len(window['events']) < 12000:
                    window['events'].append(derive(item))
                else:
                    window['truncated_events'] = window.get('truncated_events', 0)+1
    for window in windows:
        window['events'].sort(key=lambda item: item.get('clock_us', item.get('completed_clock_us', 0)))
    with path.open('rb') as source:
        sha = hashlib.file_digest(source, 'sha256').hexdigest()
    return dict(source=str(path.resolve()), sha256=sha, capture=capture,
                timestampSupport=timestamp_support, logger=logger, eventCounts=dict(counts),
                frames=frames, over18ms=over18, over20ms=over20, over30ms=over30,
                presentation={name: stream.report() for name, stream in streams.items()},
                note=('Guest completion, snapshot publication and window queue timing are distinct. '
                      'Overlapping host scopes must not be added. GPU spans include dependencies; '
                      'no physical scanout measured. Sequence gaps alone do not prove dropped display frames.'),
                incidents=windows)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--top', type=int, default=12, choices=range(1, 33))
    args = parser.parse_args()
    report = analyze(args.log, args.top)
    with args.output.open('x', encoding='utf-8') as output:
        json.dump(report, output, ensure_ascii=False, indent=2)
        output.write('\n')
    print(json.dumps({key: report[key] for key in ('frames', 'over18ms', 'over20ms', 'over30ms', 'eventCounts')}, indent=2))
