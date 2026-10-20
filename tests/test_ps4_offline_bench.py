"""Validate offline performance evidence independently of ROM or Vulkan availability."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('ps4_offline_bench', ROOT / 'scripts/ps4/run_offline_bench.py')
bench = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bench)


def fixture(samples=2, warmup=1):
    result = [dict(event='gpu_runtime_device', vendor=0x10de, device_id=1234, api_version=1, driver_version=2),
              dict(event='native_async_log', stop_requested=True, drain_completed=True,
                   dropped_oldest=0, invalid_records=0, format_failures=0, sink_failures=0,
                   pending=0, in_flight=0),
              dict(event='offline_bench_begin', schema=1, gpu_api=7, samples=samples, warmup=warmup,
                   game_execution=False, window_created=False, authored_only=True, validation=False, semaphore_wait='cv', process_priority_class=32)]
    for name, (draws, prefetches) in bench.GPU_CASES.items():
        result.append(dict(event='offline_bench_case', name=name, kind='pm4_gpu', samples=samples,
                           warmup=warmup, draws=draws, prefetches=prefetches, acquires=1,
                           acquire_after_draws=8, shader_variants=2, verified_frames=samples+warmup,
                           first_iteration_us=250))
        for index in range(samples):
            result.append(dict(event='offline_bench_sample', name=name, index=index,
                               snapshot_us=1, execute_us=30, draw_us=10, acquire_us=5, prefetch_us=4,
                               flush_us=3, other_us=2, worker_handoff_us=6,
                               completion_us=7, verification_us=9, total_us=47))
    for name, (tasks, paced) in bench.HANDOFF_CASES.items():
        count=(samples+warmup)*tasks
        result.append(dict(event='offline_bench_case', name=name, kind='semaphore', samples=samples,
                           warmup=warmup, workers=6, tasks_per_batch=tasks, cadenced=paced,
                           verified_tasks=count, verified_checksum=bench.task_checksum(count),
                           native_masks=[1,2,4,8,16,32]))
        for index in range(samples):
            result.append(dict(event='offline_bench_sample', name=name, index=index, handoff_us=10+index,
                               cadence_wait_us=16600 if paced else 1))
    result.append(dict(event='offline_bench_complete', schema=1, passed=True, cases=6))
    return result


def encoded(items):
    return '\n'.join(json.dumps(item) for item in items)


class OfflineBenchTests(unittest.TestCase):
    def test_worker_affinity_control_and_actual_inherited_masks(self):
        for mode in (None, '', 'unknown', True):
            items=fixture();items[2]['worker_affinity']=mode
            with self.subTest(mode=mode),self.assertRaises(ValueError):bench.parse_run(encoded(items),2,1)
        items=fixture();items[2]['worker_affinity']='inherited'
        for case in items:
            if case.get('kind')=='semaphore':
                case.update(creator_mask=0xff, native_masks=[0xff]*6)
        self.assertEqual(bench.parse_run(encoded(items),2,1)['identity']['worker_affinity'],'inherited')
        for field,value in [('creator_mask',None),('creator_mask',True),('creator_mask',0),('native_masks',[0xff]*5+[1])]:
            bad=copy.deepcopy(items);next(case for case in bad if case.get('kind')=='semaphore')[field]=value
            with self.subTest(field=field,value=value),self.assertRaises(ValueError):bench.parse_run(encoded(bad),2,1)

    def test_production_does_not_bind_virtual_cpu_bits_to_host_logical_processors(self):
        source=(ROOT/'backends/ps4/native_threads.cpp').read_text(encoding='utf-8')
        self.assertNotIn('SetThreadAffinityMask(',source)
        self.assertIn('"host_inherited"',source.replace('\\"','"'))
        self.assertIn('thread.native_affinity = actual.Mask;',source)

    def test_wait_primitive_is_explicit_and_cannot_be_inferred(self):
        for mode in (None, '', 'unknown', True):
            items=fixture();items[2]['semaphore_wait']=mode
            with self.subTest(mode=mode),self.assertRaises(ValueError):bench.parse_run(encoded(items),2,1)
        for mode in ('cv', 'address', 'event'):
            items=fixture();items[2]['semaphore_wait']=mode
            self.assertEqual(bench.parse_run(encoded(items),2,1)['identity']['semaphore_wait'],mode)

    def test_six_complete_cases_with_verified_tasks_and_pixels(self):
        result=bench.parse_run(encoded(fixture()),2,1)
        self.assertEqual(len(result['cases']),6)
        self.assertEqual(len(result['rows']['pm4-170-60']),2)

    def test_missing_or_duplicate_or_out_of_order_sample_is_rejected(self):
        original=fixture()
        index=next(i for i,item in enumerate(original) if item['event']=='offline_bench_sample')
        variants=[original[:index]+original[index+1:], original[:index]+[original[index]]+original[index:]]
        reversed_sample=copy.deepcopy(original);reversed_sample[index]['index']=1;variants.append(reversed_sample)
        for items in variants:
            with self.subTest(variant=len(items)),self.assertRaises(ValueError):bench.parse_run(encoded(items),2,1)

    def test_missing_completion_or_case_cannot_be_a_pass(self):
        with self.assertRaises(ValueError):bench.parse_run(encoded(fixture()[:-1]),2,1)
        items=[item for item in fixture() if item.get('name')!='handoff-14-burst']
        with self.assertRaises(ValueError):bench.parse_run(encoded(items),2,1)

    def test_phase_sum_and_nested_duration_are_checked(self):
        for field,value in [('execute_us',31),('total_us',46),('draw_us',True),('completion_us',-1)]:
            items=fixture();row=next(item for item in items if item['event']=='offline_bench_sample')
            row[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):bench.parse_run(encoded(items),2,1)

    def test_actual_workload_and_verification_count_are_required(self):
        for field,value in [('draws',0),('verified_frames',2),('shader_variants',1),('acquires',True)]:
            items=fixture();case=next(item for item in items if item['event']=='offline_bench_case')
            case[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):bench.parse_run(encoded(items),2,1)

    def test_task_checksum_and_affinity_must_be_present(self):
        for field,value in [('verified_tasks',1),('verified_checksum',1),('native_masks',[1,2,4,8,16,0])]:
            items=fixture();case=next(item for item in items if item.get('kind')=='semaphore')
            case[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):bench.parse_run(encoded(items),2,1)

    def test_failed_logger_is_not_complete_benchmark_evidence(self):
        for field,value in [('drain_completed',False),('sink_failures',1),('pending',1)]:
            items=fixture();items[1][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):bench.parse_run(encoded(items),2,1)

    def test_game_or_window_benchmark_cannot_masquerade_as_offline(self):
        for field in ('game_execution','window_created'):
            items=fixture();items[2][field]=True
            with self.subTest(field=field),self.assertRaises(ValueError):bench.parse_run(encoded(items),2,1)

    def test_types_and_device_identity_are_not_inferred(self):
        items=fixture();items[2]['schema']=True
        with self.assertRaises(ValueError):bench.parse_run(encoded(items),2,1)
        items=fixture();items[-1]['passed']=1
        with self.assertRaises(ValueError):bench.parse_run(encoded(items),2,1)
        with self.assertRaises(ValueError):bench.parse_run(encoded(fixture()[1:]),2,1)
        with self.assertRaises(ValueError):bench.parse_run(encoded(fixture()),True,1)

    def test_exact_nearest_rank_and_repeats_are_preserved(self):
        result=bench.distribution(list(range(1,101)))
        self.assertEqual((result['median_us'],result['p95_us'],result['p99_us'],result['max_us']),(50,95,99,100))
        run=bench.parse_run(encoded(fixture()),2,1)
        summary=bench.summarize([run,run])
        self.assertEqual(summary['handoff-14-burst']['handoff_us']['count'],4)
        self.assertEqual(summary['pm4-170-60']['execute_us']['mean_us'],30)

    def test_comparison_rejects_different_workload_and_reports_no_causal_claim(self):
        run=bench.parse_run(encoded(fixture()),2,1)
        current=dict(suite=bench.SUITE,parameters=dict(samples=2,warmup=1,repeats=1),host=dict(logical_processors=24),
                     gpuApi=7,device=run['device'],summary=bench.summarize([run]))
        previous=copy.deepcopy(current)
        comparison=bench.compare(current,previous)
        self.assertEqual(comparison['pm4-170-60']['execute_us']['mean_us']['change_percent'],0)
        for field,value in [('suite','different'),('parameters',{}),('host',{}),('gpuApi',6),('device',{})]:
            bad=copy.deepcopy(previous);bad[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):bench.compare(current,bad)

    def test_native_fixture_uses_maintained_paths_without_guest_or_window_entry(self):
        source=(ROOT/'backends/ps4/native_offline_bench.cpp').read_text(encoding='utf-8')
        for required in ('ExecuteNativeGpuCommands(', 'gpu.Drain()', 'ReadColorForDiagnostics(',
                         'ResolveSemaphoreImport(', 'NativeHostAffinityForDiagnostics(',
                         'WaitFlipOpportunity()', 'verified_checksum', 'pool.Stop()'):
            self.assertTrue(required in source,required)
        for forbidden in ('ConfigureNativeUnpacedPresentation(', 'CreateWindow', 'PresentNativeVideoGpu(',
                          'IsCollectionLaunch(', 'LoadLibraryA(', 'TerminateProcess('):
            self.assertFalse(forbidden in source,forbidden)


if __name__=='__main__':
    unittest.main()
