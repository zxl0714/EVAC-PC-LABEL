"""Summarize module lifecycle and output-audio telemetry without executing a game."""

import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import re

from project import write_json_new
from analysis_paths import private_output


def distribution(values):
    if not values:
        return None
    ordered = sorted(values)
    return dict(samples=len(values), first=values[0], last=values[-1],
                minimum=ordered[0], maximum=ordered[-1],
                median=ordered[(len(ordered)-1)//2],
                p95=ordered[max(0, math.ceil(len(ordered)*.95)-1)])


def finite_sample(fields, key):
    try:
        value = float(fields[key])
    except (KeyError, ValueError):
        return None
    return value if math.isfinite(value) and value >= 0 else None


def analyze(lines):
    loads, unloads = Counter(), Counter()
    active, unmatched = set(), []
    audio = defaultdict(lambda: defaultdict(list))
    fatal, errors, markers, closed, count = [], 0, 0, False, 0
    queue_heads, cue_requests, trace_scopes = [], [], set()
    gameplay = Counter()
    dropped_max = None
    for line in lines:
        count += 1
        event = re.search(r"MODULE_(LOAD|UNLOAD) path='([^']+)'", line)
        if event:
            name = re.split(r'[/\\]', event[2])[-1].lower()
            if event[1] == 'LOAD':
                loads[name] += 1
                if name in active:
                    unmatched.append(dict(event='duplicate-load', module=name, line=count))
                active.add(name)
            else:
                unloads[name] += 1
                if name not in active:
                    unmatched.append(dict(event='unmatched-unload', module=name, line=count))
                active.discard(name)
        if '[error]' in line:
            errors += 1
        if re.search(r'\[critical\]|\bFATAL\b|Unhandled exception', line, re.IGNORECASE):
            if len(fatal) < 16:
                fatal.append(dict(line=count, text=line.strip()[:512]))
        if 'USER_MARKER ' in line:
            markers += 1
        if 'User requested close' in line:
            closed = True
        if not closed and ('AUDIO_GUEST ' in line or 'AUDIO_GAMEPLAY ' in line):
            fields = dict(re.findall(r'\b(\w+)=([^\s]+)', line))
            if 'AUDIO_GUEST ready ' in line and fields.get('scope'):
                trace_scopes.add(fields['scope'])
            if 'AUDIO_GUEST summary ' in line:
                dropped = finite_sample(fields, 'dropped_total')
                if dropped is not None and dropped.is_integer():
                    dropped_max = max(dropped_max or 0, int(dropped))
            if 'AUDIO_GUEST event ' in line:
                if fields.get('code') == '30' and fields.get('matched') == 'true':
                    age = finite_sample(fields, 'queue_age_ms')
                    if age is not None:
                        queue_heads.append(age)
                if fields.get('code') == '43':
                    elapsed = finite_sample(fields, 'elapsed_ms')
                    if elapsed is not None:
                        cue_requests.append(elapsed)
            if 'AUDIO_GAMEPLAY event ' in line and fields.get('capture_valid') == 'true':
                phase = fields.get('phase')
                if phase in ('power_action_begin', 'power_action_end', 'actor_state3_death_phase'):
                    gameplay[phase] += 1
        if closed or 'AUDIO_DIAG stream ' not in line:
            continue
        fields = dict(re.findall(r'\b(\w+)=([^\s]+)', line))
        driver = fields.get('driver', 'unknown')
        for key in ('queue_ms', 'oldest_ms', 'sdl_input_ms', 'consumed_age_max_ms',
                    'silence_blocks', 'submit_gap_max_ms', 'callback_gap_max_ms'):
            try:
                value = float(fields[key])
            except (KeyError, ValueError):
                continue
            if math.isfinite(value) and value >= 0:
                audio[driver][key].append(value)
    return dict(format=1, guestExecution=False, lines=count,
                modules={name: dict(loads=loads[name], unloads=unloads[name])
                         for name in sorted(loads.keys() | unloads.keys())},
                activeModules=sorted(active), lifecycleWarnings=unmatched,
                fatalLines=fatal, errorLevelLines=errors, userMarkers=markers,
                closeRequested=closed,
                audio={driver: {key: distribution(values) for key, values in fields.items()}
                       for driver, fields in audio.items()},
                guestAudio=dict(traceScopes=sorted(trace_scopes),
                    matchedQueueHeadAgeMs=distribution(queue_heads),
                    cueRequestMs=distribution(cue_requests),
                    queueHeadsOverMs={str(limit): sum(age > limit for age in queue_heads)
                                      for limit in (20, 100, 1000)},
                    gameplayPhaseObservations=dict(gameplay),
                    droppedEventsMaximum=dropped_max,
                    measurementScope='Observed matched guest queue heads and cue-request API completion; not audible onset. Head retries may be sampled repeatedly. Missing probes and dropped events limit coverage; cue names are not pickup proof.'),
                measurementScope='Output backend samples before user close; not guest sound-command latency or audible A/V synchronization',
                logCompleteness='Unknown; not inferred from the absence of fatal messages')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    path = args.run / 'game.log' if args.run.is_dir() else args.run
    if path.is_symlink() or path.is_junction():
        raise ValueError('Read a regular log copy, not a linked path')
    output = private_output(args.output, path) if args.output else None
    with path.open(encoding='utf-8', errors='replace') as stream:
        result = analyze(stream)
    if output:
        write_json_new(output, result)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
