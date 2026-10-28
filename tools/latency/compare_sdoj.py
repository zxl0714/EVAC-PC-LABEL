"""Join current SDOJ and Xenia evidence, retaining historical scope."""
import argparse
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from input_latency_report import distribution
from project import digest, write_json_new
from sdoj_xenia_latency import analyze
from latency_probe import read_trace
from evac_sdoj_latency import analyze as analyze_native


def current_native(base):
    runs = {}
    for filename, label in (('evac-sdoj-onev-cycle-r2.json', 'pixels-on'),
                            ('evac-sdoj-onev-cycle-r3.json', 'pixels-disabled')):
        path = base / filename
        report = checked(path, ROOT)
        paths = [Path(s['path']) for s in report['sources']]
        raw_path, = [p for p in paths if p.suffix == '.csv']
        logs = [p for p in paths if p.suffix == '.log']
        lineage_path, = [p for p in paths if p.name.startswith('evac-sdoj-output-')]
        clock_path, = [p for p in paths if p.name == 'clock-check-r1.json']
        fresh = analyze_native(read_trace(raw_path.read_text()),
            [p.read_text(encoding='utf-8') for p in logs],
            json.loads(clock_path.read_text()), checked(lineage_path, ROOT))
        if fresh['samples'] != report['samples'] or fresh['issues']:
            raise ValueError('Native earliest-core/output validation differs')
        runs[label] = dict(capture=report['native_capture'], paired=report['paired_sources'],
            requested=report['requested_sources'], statistics=report['statistics'],
            mapped_masks=sorted({s['mapped_buttons'] for s in report['samples']}),
            report=dict(path=str(path), sha256=digest(path)))
    receipts = [base / f'evac-sdoj-r{n}-launch.json' for n in (1, 2)]
    launches = [json.loads(p.read_text()) for p in receipts]
    for key in ('executable_sha256', 'config_sha256', 'stage_sha256'):
        if len({r[key] for r in launches}) != 1:
            raise ValueError('Native ON/OFF component or config differs')
    review_path = base / 'evac-sdoj-manual-first-response-r1.json'
    review = checked(review_path, ROOT)
    return dict(runs=runs, same_binary_and_config=True,
        launch_receipts=[dict(path=str(p), sha256=digest(p)) for p in receipts],
        manual_response=dict(path=str(review_path), sha256=digest(review_path),
            accepted=review['accepted_responses'],
            core_to_present_ms=review['game_input_to_response_present_ms'], p95_ms=None))


def checked(path,root):
    report=json.loads(path.read_text())
    for item in report.get('sources',[]):
        source=Path(item['path'])
        if not source.is_absolute():
            source=root/source
        if digest(source)!=item['sha256']:
            raise ValueError('Original evidence identity changed: '+str(source))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--current-native', action='store_true')
    args = parser.parse_args()
    base=ROOT/'.local/input-latency-v080-r1'
    native_path=base/'sdoj-onev-paired-r1.json'
    native=checked(native_path,ROOT)
    points=[r['points_us'] for r in native['responses'] if r['selection']=='gameplay' and r['lineage_verified']]
    native_current=dict(count=len(points),
        core_to_present_begin=distribution([p['present_begin']-p['logic_copy'] for p in points],60),
        core_to_present_return=distribution([p['present_return']-p['logic_copy'] for p in points],60),
        report=dict(path=str(native_path),sha256=digest(native_path)),
        first_response_pixel_verified=False,source_commit='c427257bdf45e0e5b57087108739e16342eb894c')
    runs={}
    for name,label in (('xenia-sdoj-cycle-r2.json','pixels-disabled'),('xenia-sdoj-cycle-r3.json','pixels-on')):
        path=base/name
        report=checked(path,ROOT)
        trace=read_trace(Path(report['source']['path']).read_text())
        fresh=analyze(trace)
        if fresh['samples']!=report['samples']:
            raise ValueError('Earliest-consumption/ownership validation changed')
        runs[label]=dict(pid=trace['pid'],records=trace['committed'],complete=trace['complete'],
            paired=report['paired_sources'],statistics=report['statistics'],
            report=dict(path=str(path),sha256=digest(path)))
    review_path=base/'xenia-sdoj-manual-first-response-r1.json'
    review=checked(review_path,ROOT)
    old_root=Path('E:/Projects/CAVE-PC-Collection')
    history={}
    for mode,label in ((1,'low'),(2,'balanced'),(3,'original')):
        path=old_root/'.local/analysis'/f'sdoj-map-init-r1-{label}-stage.json'
        report=checked(path,old_root)
        group,=[g for g in report['groups'] if g['depth']==1]
        run_metadata=old_root/report['run']/'run.json'
        metadata=json.loads(run_metadata.read_text())
        for item in metadata['gameInputs']:
            if digest(ROOT/'game-data/sdoj'/item['path'])!=item['sha256']:
                raise ValueError('Historical/current original game program differs')
        history[mode]=dict(first_output_chains=group['proven_first_results'],
            core_to_present_return_us=group['intervals_us']['copy_to_present_return'],
            source_commit=metadata.get('sourceCommit'),
            report=dict(path=str(path),sha256=digest(path)),
            metadata=dict(path=str(run_metadata),sha256=digest(run_metadata)),
            same_game_inputs_as_current=True,current_build_measurement=False)
    result=dict(scene='Arcade HD / Training / Stage1 / Type-A / Shot',
        native_current_onev=current_native(base) if args.current_native else native_current,
        native_prior_pilot=native_current,xenia_original_threev=runs,
        xenia_manual_response=dict(path=str(review_path),sha256=digest(review_path),
            accepted=review['accepted_responses'],core_to_present_ms=review['game_input_to_response_present_ms']),
        historical_native=history,
        boundary='Native core-before-copy hook88051D74; Xenia original input load after88051D84, observed88051D88. Present begin/return distinguished. No physical frontend or photon result.',
        controls='Identical Xenia binary; R2 pixel configuration was incomplete, so no GPU pixel copies. R3 valid ON. Both retain3V. Native ON/OFF uses identical binary/config;35 first-core chains each. Gameplay start phase differs; no zero-cost or subms winner claim.',
        mapping_scope='Same synthetic direction+x raw masks. Native mapped masks24/20 versus Xenia72/68 reflect profile extra-button mappings. Direction bits and first directional response verified; shooting workload not identical.',
        rejected=dict(r1='Missing synthetic-pad environment: startup only, no input response',
            automatic_pixel_tracking='All35 rejected at the unchanged NCC confidence gate; explicit manual image acceptance limited to command101'),
        next_required=([] if args.current_native else ['Current native first-responsive-image evidence']) + [
                       'Guwange and four PS4 internal/emulator response comparisons',
                       'physical/frontend and current display evidence as separately marked gaps'],
        display_reference='144Hz / screen center / theory; unknown panel overhead D retained')
    output=base/('sdoj-comparison-r2.json' if args.current_native else 'sdoj-comparison-partial-r1.json')
    write_json_new(output,result)
    print(json.dumps(dict(output=str(output),sha256=digest(output),native_current=result['native_current_onev']),indent=2))


if __name__=='__main__':
    main()
