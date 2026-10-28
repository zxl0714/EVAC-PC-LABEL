"""Revalidate earliest-core provenance and summarize the four native G2 runs."""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from evac_futari_latency import merge_output
from g2_latency import analyze
from latency_probe import read_trace
from pixel_probe import read_capture
from project import digest, write_json_new


def main():
    base = ROOT / '.local/input-latency-v080-r1'
    clock_path = base / 'clock-check-r1.json'
    clock = json.loads(clock_path.read_text())
    cases = ((2, 'evac-g2-cycle-r3.json', 'twov-on'),
             (3, 'evac-g2-onev-cycle-r1.json', 'onev-on'),
             (4, 'evac-g2-onev-pixel-off-r1.json', 'onev-off'),
             (5, 'evac-g2-twov-pixel-off-r1.json', 'twov-off'))
    summary = {}
    for run, previous_name, label in cases:
        prior = json.loads((base / previous_name).read_text())
        sources = [Path(p['path']) for p in prior['sources']]
        for item in prior['sources']:
            if digest(Path(item['path'])) != item['sha256']:
                raise ValueError('Original capture/log identity changed')
        trace_path = base / f'evac-g2-probe-run-r{run}.csv'
        trace = read_trace(trace_path.read_text())
        logs = [p for p in sources if p.suffix == '.log']
        report = analyze(merge_output(trace, [p.read_text(encoding='utf-8') for p in logs], clock))
        if [s['points'] for s in report['samples']] != [s['points'] for s in prior['samples']]:
            raise ValueError('Earliest-core validation changes old selected samples')
        report['native_capture'] = {k:trace[k] for k in ('pid','committed','complete','overflow','incomplete')}
        report['sources'] = prior['sources']
        report['earliest_matching_core_required'] = True
        report['analyzer_sha256'] = digest(ROOT / 'scripts/g2_latency.py')
        output = base / f'evac-g2-final-{label}-r1.json'
        if output.exists():
            if json.loads(output.read_text()) != json.loads(json.dumps(report)):
                raise ValueError('Existing final report differs; preserve it and use a new revision')
        else:
            write_json_new(output,report)
        summary[label] = dict(pid=trace['pid'],records=trace['committed'],
            paired=report['paired_sources'],requested=report['requested_sources'],
            modes=report['modes'],statistics=report['statistics'],
            report=dict(path=str(output),sha256=digest(output)))
    pixel = {}
    for run, label, names in ((2,'twov',('g2-first-response-r1.json','g2-first-response-r2.json')),
                             (3,'onev',('g2-onev-first-response-r1.json','g2-onev-first-response-r2.json'))):
        capture = read_capture(base / f'evac-g2-pixels-r{run}')
        checks=[]
        for name in names:
            report_path=base/name
            r=json.loads(report_path.read_text())
            for s in r['sources']:
                if digest(Path(s['path']))!=s['sha256']:
                    raise ValueError('Pixel analysis input identity changed')
            checks.append(dict(path=str(report_path),sha256=digest(report_path),
                accepted=len(r['accepted']),excluded=len(r['excluded']),
                offsets=sorted({s['frame_offset'] for s in r['accepted']}),statistics=r['statistics']))
        pixel[label]=dict(header=capture['header'],footer=capture['footer'],checks=checks)
    components={}
    for fixture in ('evac-g2-r2','evac-g2-r3'):
        folder=base/fixture/'collection'
        components[fixture]={str(p.relative_to(folder)):digest(p) for p in (
            folder/'runtime/xbox360/espgaluda2.exe',folder/'runtime/games/espgaluda2/espgaluda2_dat12.dll',
            folder/'runtime/xbox360/rexruntime.dll')}
    if list(components.values())[0]!=list(components.values())[1]:
        raise ValueError('1V/2V fixtures do not contain identical measured binaries')
    result=dict(scene='Xbox360 / Play / Stage1 / Ageha / 1920x1080 output, window 1280x720',
        input='Synthetic right+x / left+x; hold60ms gap97ms; not a physical device measurement',
        native=summary,pixel=pixel,components=components,
        controls='Pixel OFF uses the identical binaries, successful gameplay sources only; different gameplay start offsets. Supports no extra whole cycle, not zero observer cost.',
        still_required=['Xenia corresponding-response comparison','physical_or_Windows_frontend','current_same_response_display','144Hz_center_pixel_light_model'])
    write_json_new(base/'g2-native-comparison-r1.json',result)
    print(json.dumps({label:dict(paired=r['paired'], core=r['statistics']['game_input_to_present_begin_us'])
                      for label,r in summary.items()},indent=2))


if __name__=='__main__':
    main()
