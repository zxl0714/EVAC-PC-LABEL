"""Compare two inspected Ketsui responses without turning them into medians."""
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from project import digest,write_json_new


def checked(path):
    report=json.loads(path.read_text())
    for source in report['sources']:
        if digest(Path(source['path']))!=source['sha256']:
            raise ValueError('Inspected response evidence changed: '+source['path'])
    if report['accepted_responses']!=1 or report['command']!=101 or report['p95_ms'] is not None:
        raise ValueError('One inspected response per runtime required')
    return report


def main():
    base=ROOT/'.local/input-latency-v080-r1'
    native_path=base/'native-ketsui-manual-first-response-r3.json'
    shad_path=base/'shad-ketsui-manual-first-response-r1.json'
    native,shad=(checked(p) for p in (native_path,shad_path))
    control=base/'ketsui-present-pixel-control-r1.json'
    control_report=json.loads(control.read_text())
    for source in control_report['sources']:
        if digest(Path(source['path']))!=source['sha256']: raise ValueError('Native presentation control changed')
    keys=('core_delivery_to_response_present_ms','pad_return_to_response_present_ms',
          'synthetic_source_to_response_present_ms')
    rows=[]
    for name,result in (('native',native),('shadps4',shad)):
        rows.append(dict(runtime=name,accepted_responses=1,p95_ms=None,
            first_response_snapshot=result['first_response_snapshot'],
            first_submitted_response_snapshot=result['first_submitted_response_snapshot'],
            **{key:result[key] for key in keys}))
    report=dict(scene='Ketsui / original same eboot and modules / ARCADE / Type-A / Stage1',
        scope='Two separately inspected software responses at the same input-delivery and CPU Present boundaries',
        runtimes=rows,differences_native_minus_shad_ms={key:native[key]-shad[key] for key in keys},
        differences_are_paired_examples_not_population_medians=True,
        same_exact_stage_position_or_poll_phase=False,
        readiness_points_are_not_equivalent='Native after pacing/copied fence; shad after draw submission',
        display_reference='144 Hz / screen center / theoretical pixel-light budget, panel response unknown',
        conclusion='The native example loses its first response snapshot before Present; shad submits its first response. This locates queue age but does not prove a fixed per-title latency advantage.',
        unmeasured=['physical_device_frontend','actual_emulated_CPU_port_read','OS_display','panel_photons',
                    'shad_same_build_pixel_off_control','first_response_population_P95','remaining_three_PS4_titles'],
        sources=[dict(path=str(p),sha256=digest(p)) for p in (Path(__file__),native_path,shad_path,control)])
    output=base/'ketsui-comparison-r1.json';write_json_new(output,report)
    print(json.dumps(dict(runtimes=rows,differences=report['differences_native_minus_shad_ms'],sha256=digest(output)),indent=2))


if __name__=='__main__': main()
