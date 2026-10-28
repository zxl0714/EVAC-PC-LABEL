"""Collect the six requested latency rows and audit their retained evidence.

This performs no gameplay, collection, rebuilding, publication or image editing.
Verified response subsets, internal controls and display theory remain distinct.
"""
import argparse
import json
import math
from pathlib import Path
import statistics
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from input_latency_report import distribution
from project import digest,write_json_new


def screen_budget(hz,mode,position=.5):
    if not math.isfinite(hz) or hz<=0 or not 0<=position<=1:
        raise ValueError('Positive refresh rate and valid screen position required')
    period=1000/hz
    if mode=='immediate':mean,low,high=period/2,0,period
    elif mode=='empty_fifo':mean,low,high=period/2+position*period,position*period,(1+position)*period
    else:raise ValueError('Explicit presentation model required')
    return dict(refresh_hz=hz,screen_fraction=position,mode=mode,mean_ms=mean,
        ideal_phase_min_ms=low,ideal_phase_max_ms=high,panel_delay_ms=None,
        origin='GPU-ready image that the presentation engine can adopt, not CPU Present',
        assumptions='Fixed progressive scan approximately one refresh period, uniform arrival phase, no extra queue, no VRR',
        evidence_type='THEORY')


def pixel_result(record):
    samples=record['accepted']
    if not samples:raise ValueError('At least one verified response required')
    values={'input_available_to_core':[],'core_to_response_present':[],'input_available_to_response_present':[]}
    for sample in samples:
        total=sample['source_to_response_present_us'];core=sample['core_to_response_present_us']
        if not 0<=core<=total:raise ValueError('Paired response clocks differ')
        values['input_available_to_core'].append(total-core)
        values['core_to_response_present'].append(core)
        values['input_available_to_response_present'].append(total)
    return dict(sample_count=len(samples),statistic='median of pixel-verified subset',
        **{key:dict(distribution(value,60),mean_ms=statistics.mean(value)/1000) for key,value in values.items()})


def manual_result(record,core_key,total_key):
    if record['accepted_responses']!=1:raise ValueError('Expected explicitly reviewed single response')
    core,total=record[core_key],record[total_key]
    if not 0<=core<=total:raise ValueError('Paired manual response clocks differ')
    def point(value):return dict(count=1,median_ms=value,median_game_frames=value*.06,p95_ms=None,mean_ms=value)
    return dict(sample_count=1,statistic='one manually inspected response, not a population median',
        input_available_to_core=point(total-core),core_to_response_present=point(core),
        input_available_to_response_present=point(total))


class Evidence:
    def __init__(self):self.files={};self.reports={}

    def checked_file(self,path,expected=None,parent=ROOT):
        path=Path(path)
        if not path.is_absolute():
            candidates=[p for p in (parent/path,ROOT/path) if p.is_file()]
            if not candidates:raise ValueError('Missing retained evidence: '+str(path))
            path=candidates[0]
        path=path.resolve(strict=True)
        if path not in self.files:self.files[path]=digest(path)
        if expected is not None and self.files[path]!=expected.lower():
            raise ValueError('Retained evidence identity changed: '+str(path))
        return path

    def report(self,path,expected=None):
        path=self.checked_file(path,expected)
        if path in self.reports:return self.reports[path]
        value=json.loads(path.read_text(encoding='utf-8-sig'));self.reports[path]=value
        # Follow declared source/report edges, not incidental strings or stored
        # historical "still_required" notes in immutable intermediate reports.
        declared=value.get('sources',[])
        if isinstance(declared,dict):declared=[]
        if isinstance(value.get('source'),dict) and {'path','sha256'}<=value['source'].keys():
            declared=[*declared,value['source']]
        for item in declared:
            source=self.checked_file(item['path'],item['sha256'],path.parent)
            if source.suffix.lower()=='.json':self.report(source,item['sha256'])
        return value


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();base=ROOT/'.local/input-latency-v080-r1'
    if not args.output.resolve().is_relative_to(base) or args.output.exists():raise ValueError('Fresh private report required')
    evidence=Evidence();evidence.checked_file(Path(__file__))
    def load(name):return evidence.report(base/name)
    specs=[
        ('futari','Ver1.5 / Xbox360 / Training / Stage1 / Original / Reco Normal','1V / 1V',
         'futari-first-response-r3.json','xenia-futari-first-response-r2.json','pixel',
         'Original gameplay input read; 60Hz FrameDelay1 confirmed in both'),
        ('espgaluda2','Xbox360 / Play / Stage1 / Ageha','default2V / original2V',
         'g2-first-response-r1.json','xenia-g2-first-response-r1.json','pixel',
         'Original game input load; no additional output offset in accepted images'),
        ('espgaluda2-experimental','Xbox360 / Play / Stage1 / Ageha','experimental1V / original2V',
         'g2-onev-first-response-r1.json','xenia-g2-first-response-r1.json','pixel',
         'Optional native1V, not the default; original input load'),
        ('sdoj','Arcade HD / Training / Stage1 / Type-A / Shot','current1V / original3V',
         'evac-sdoj-manual-first-response-r1.json','xenia-sdoj-manual-first-response-r1.json','manual360',
         'Native core input-copy entry versus Xenia original load completion; different extra-button mappings'),
        ('guwange','XBLA0.0.0.4 / Arcade / Shishin / Stage1','original / original',
         'native-guwange-manual-first-response-r1.json','xenia-guwange-manual-first-response-r1.json','manual360',
         'Original arcade P1 port load; first response is two outputs after core-owned output'),
        ('ketsui','ARCADE / Type-A / Stage1 / original01.02','native VSync / shad FIFO',
         'native-ketsui-manual-first-response-r3.json','shad-ketsui-manual-first-response-r1.json','manualps4',
         'Original integration input-delivery return, not emulated arcade CPU port read'),
        ('daioujou','ARCADE / Type-A / Shotia / Stage1A / original01.04','native VSync / shad FIFO',
         'native-daioujou-manual-first-response-r1.json','shad-daioujou-manual-first-response-r1.json','manualps4',
         'Original integration input-delivery return; native/shad callback cadence16.6685/33.3894ms'),
    ]
    rows=[]
    for game,scene,policy,native_name,emulator_name,kind,boundary in specs:
        values=[]
        for name in (native_name,emulator_name):
            record=load(name)
            if kind=='pixel':value=pixel_result(record)
            elif kind=='manual360':value=manual_result(record,'game_input_to_response_present_ms','source_to_response_present_ms')
            else:value=manual_result(record,'core_delivery_to_response_present_ms','synthetic_source_to_response_present_ms')
            value['receipt']=dict(path=str(base/name),sha256=evidence.files[(base/name).resolve()]);values.append(value)
        native,emulator=values
        rows.append(dict(game=game,scene=scene,policies=policy,input_boundary=boundary,
            native=native,emulator=emulator,
            native_minus_emulator_core_ms=native['core_to_response_present']['median_ms']-emulator['core_to_response_present']['median_ms'],
            differences_are_population_effects=False,
            screen_model_modes=['empty_fifo'] if kind=='manualps4' else ['immediate','empty_fifo'],
            physical_button_to_light_measured=False))
    # Internal distributions and ON/OFF controls remain separate from the smaller
    # pixel-verified populations used in the final main table.
    controls=[
        'evac-futari-pixel-off-r1.json','xenia-futari-pixel-off-r1.json',
        'g2-native-comparison-r1.json','g2-comparison-r1.json','sdoj-comparison-r2.json',
        'guwange-comparison-r1.json','ketsui-comparison-r1.json','shad-ketsui-pixel-control-r1.json',
        'daioujou-comparison-r1.json','daioujou-native-pixel-control-r1.json','daioujou-shad-pixel-control-r1.json',
        'futari-window-front-r2.json','xenia-futari-window-events-r3.json',
        'futari-historical-present-display-r1.json','xenia-futari-historical-present-display-r1.json',
    ]
    for name in controls:load(name)
    # The G2 aggregate uses report links rather than a sources array.
    for name in ('g2-native-comparison-r1.json','g2-comparison-r1.json'):
        report=load(name)
        for case in report.get('native',{}).values():
            ref=case['report'];evidence.report(ref['path'],ref['sha256'])
        for case in report.get('xenia',{}).get('runs',{}).values():
            ref=case['report'];evidence.report(ref['path'],ref['sha256'])
    for relative in ('third_party/rexglue/src/input/sdl/sdl_input_driver.cpp',
        'backends/ps4/input/sdl_gamepad.cpp','backends/ps4/native_pad.cpp',
        '.local/input-latency-v080-r1/shadps4-source/src/sdl_window.cpp',
        '.local/input-latency-v080-r1/shadps4-source/src/input/controller.cpp'):
        evidence.checked_file(ROOT/relative)
    runtime_stages=[]
    for relative in ('evac-futari-r3/collection','evac-g2-r2/collection','evac-g2-r3/collection',
        'evac-sdoj-r1/collection','evac-guwange-r2/collection','evac-ps4-r9/collection',
        'evac-daioujou-r2/collection','shad-ps4-r3','shad-daioujou-r1',
        'xenia-g2-r1','xenia-sdoj-r1','xenia-guwange-r2'):
        folder=base/relative;path=evidence.checked_file(folder/'probe-stage.json')
        stage=json.loads(path.read_text());components=[]
        entries=stage.get('binaries',stage.get('files',[]))
        if isinstance(entries,list):
            for item in entries:
                binary=evidence.checked_file(folder/item['path'],item['sha256'])
                components.append(dict(path=str(binary),sha256=evidence.files[binary]))
        if 'executable' in stage and 'exe_sha256' in stage:
            binary=evidence.checked_file(stage['executable'],stage['exe_sha256'])
            components.append(dict(path=str(binary),sha256=evidence.files[binary]))
        if not components:raise ValueError('Stage has no independently checked runtime components: '+relative)
        runtime_stages.append(dict(path=str(path),sha256=evidence.files[path],components=components))
    # Futari shares the exact Xenia pixel executable subsequently staged for
    # G2/SDOJ/Guwange. Its dedicated build receipt pins the same executable.
    build=load('xenia-futari-pixels-build-r1/build-result.json')
    evidence.checked_file(base/'xenia-futari-pixels-build-r1/xenia_canary.exe',build['exe_sha256'])
    result=dict(active_games=['futari','espgaluda2','sdoj','guwange','ketsui','daioujou'],
        held_games=['esprade','feveron'],rows=rows,
        displayed_frame_unit_ms=1000/60,frame_unit_is_not_strategy_V=True,
        theory={mode:screen_budget(144,mode) for mode in ('immediate','empty_fifo')},
        physical_total='device-to-core H + measured representative core-to-response-Present B + unmeasured post-Present-to-adoptable-image R + ideal144Hz scan budget S + unknown panel D',
        direct_software_total='Each accepted response is paired directly from synthetic published state to its first responsive image Present; not the sum of separate medians',
        physical_total_ms=None,physical_total_is_unmeasured=True,
        runtime_stages=runtime_stages,
        emulator_source_versions=dict(xenia='291f0c9be398a3e0a0e29f378cbd564fa28c55a9',
            shadps4='529ceb32f5546b820588f194b903cc39d08015fe / v0.20.0'),
        primary_scope='Representative software responses and code/theory decomposition, not all modes, a population hardware ranking, or a same-response photon measurement',
        evidence_files=[dict(path=str(path),sha256=identity) for path,identity in sorted(evidence.files.items())],
        checked_report_count=len(evidence.reports))
    write_json_new(args.output,result)
    print(json.dumps(dict(output=str(args.output),sha256=digest(args.output),
        checked_reports=len(evidence.reports),checked_files=len(evidence.files),
        rows=[dict(game=r['game'],native=r['native']['core_to_response_present'],emulator=r['emulator']['core_to_response_present'],
            phase=[r[k]['input_available_to_core']['median_ms'] for k in ('native','emulator')],
            direct_total=[r[k]['input_available_to_response_present']['median_ms'] for k in ('native','emulator')],
            difference_ms=r['native_minus_emulator_core_ms']) for r in rows]),indent=2))


if __name__=='__main__':main()
