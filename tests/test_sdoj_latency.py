from pathlib import Path
import re
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from sdoj_latency import analyze, records, input_records, direction_edges, run_logs
from project import ROOT


def event(sequence, phase, **values):
    return dict(seq=sequence, phase=phase, t_us=sequence * 100, thread=1, module=1,
                generation=1, host_iteration=10, engine_frame=100, epoch=10,
                copies=1, batches=1, argument=0x90000000, writer=0xffffffff,
                published=0x90000000, swap_serial=7, cursor=0x90001000,
                current0=8, current1=0, consumed0=0, consumed1=0,
                valid=True, swap_stable=True, rejected=0, limit=65536) | values


def chain():
    return [event(1, 'logic_copy'), event(2, 'commands_begin'),
            event(3, 'commands_end'), event(4, 'swap_return'),
            event(5, 'render_source', thread=2, host_iteration=11, epoch=11),
            event(6, 'render_return', thread=2, host_iteration=11, epoch=11)]


class SdojLatencyTests(unittest.TestCase):
    def test_pause_repeated_picture_is_not_another_new_input_result(self):
        data = chain() + [event(7, 'render_source', thread=2, host_iteration=50, epoch=50, t_us=100000),
                          event(8, 'render_return', thread=2, host_iteration=50, epoch=50, t_us=100100)]
        report = analyze(data)
        self.assertEqual(report['paired_command_buffers'], 2)
        self.assertEqual(report['first_result_reads'], 1)
        self.assertEqual(report['held_result_reads'], 1)
        self.assertEqual(report['first_result_intervals_us']['copy_to_source']['max'], 400)
        self.assertEqual(report['first_result_copy_to_source_epochs'], {1: 1})
        self.assertEqual([row['first_result_read'] for row in report['lineages']], [True, False])

    def test_ordered_exact_buffer_chain_has_stage_durations(self):
        report = analyze(chain())
        self.assertEqual(report['paired_command_buffers'], 1)
        self.assertTrue(report['continuous_trace'])
        self.assertEqual(report['intervals_us']['copy_to_source']['median'], 400)
        self.assertEqual(report['copy_to_source_host_iterations'], {1: 1})
        self.assertIn('not physical scanout', report['scope'])

    def test_pointer_equality_without_publication_is_not_lineage(self):
        data = [row for row in chain() if row['phase'] != 'swap_return']
        self.assertEqual(analyze(data)['paired_command_buffers'], 0)

    def test_incomplete_list_or_wrong_source_cannot_pair(self):
        for index, changes in ((2, {'cursor': 0x90000000}),
                               (2, {'copies': 2}),
                               (4, {'argument': 0x90010000}),
                               (4, {'swap_serial': 8}),
                               (4, {'generation': 2}),
                               (4, {'swap_stable': False})):
            with self.subTest(index=index, changes=changes):
                data = chain()
                data[index].update(changes)
                self.assertEqual(analyze(data)['paired_command_buffers'], 0)

    def test_reused_address_invalidates_earlier_publication(self):
        data = chain()
        data[4:4] = [event(7, 'commands_begin', t_us=450, batches=2)]
        self.assertEqual(analyze(sorted(data, key=lambda row: row['t_us']))['paired_command_buffers'], 0)

    def test_reuse_during_render_also_invalidates_the_chain(self):
        data = chain()
        data[5:5] = [event(7, 'commands_begin', t_us=550, batches=2)]
        self.assertEqual(analyze(data)['paired_command_buffers'], 0)

    def test_lost_event_inside_a_lineage_prevents_age_attribution(self):
        data = chain()
        for row in data[2:]:
            row['seq'] += 1
        report = analyze(data)
        self.assertEqual(report['paired_command_buffers'], 0)
        self.assertEqual(report['issues']['lineage_crosses_lost_observation'], 1)

    def test_missing_prefix_or_duplicate_records_are_not_continuous(self):
        data = chain()
        for row in data:
            row['seq'] += 10
        self.assertFalse(analyze(data)['continuous_trace'])
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            analyze([*chain(), event(1, 'host_begin')])

    def test_observer_loss_or_budget_end_prevents_continuity_claim(self):
        for changes in ({'seq': 100}, {'rejected': 1}, {'limit': 6}):
            with self.subTest(changes=changes):
                data = chain()
                data[-1].update(changes)
                self.assertFalse(analyze(data)['continuous_trace'])

    def test_parsing_hex_and_logging_order(self):
        parsed = records([
            'SDOJ_LATENCY seq=2 t_us=20 phase=render_source thread=1 host_iteration=2 argument=88001234 valid=true',
            'unrelated record',
            'SDOJ_LATENCY seq=1 t_us=10 phase=host_begin thread=2 host_iteration=1',
        ])
        self.assertEqual([row['seq'] for row in parsed], [1, 2])
        self.assertEqual(parsed[1]['argument'], 0x88001234)
        self.assertIs(parsed[1]['valid'], True)

    def test_rotated_logs_use_numeric_oldest_first_and_ignore_other_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('game.log', 'game.2.log', 'game.10.log', 'other.log', 'game.1.log'):
                (root / name).write_text('self-authored fixture', encoding='utf-8')
            self.assertEqual([path.name for path in run_logs(root)],
                             ['game.10.log', 'game.2.log', 'game.1.log', 'game.log'])
            self.assertEqual(run_logs(root / 'other.log'), [root / 'other.log'])

    def test_input_direction_edge_joins_only_proven_buffer_lineage(self):
        inputs = input_records([
            'INPUT_DIAG key=102 down=true repeat=false before=0000 after=0008 t_us=10 frame=9',
            'SDOJ_INPUT phase=button_copy t_us=20 frame=10 user=0 buttons=0008',
            'SDOJ_INPUT phase=core_mask_built t_us=30 frame=10 module=1 generation=1 current0=00000008 current1=00000000',
        ])
        data = chain()
        edges = direction_edges(inputs, analyze(data), data)
        self.assertEqual(len(edges), 1)
        self.assertEqual(edges[0]['intervals_us']['key_to_renderer_source'], 490)
        self.assertTrue(edges[0]['live_player_requires_external_observation'])
        self.assertEqual(edges[0]['lineage']['copy']['seq'], 1)
        for row in data[2:]:
            row['seq'] += 1
        self.assertIsNone(direction_edges(inputs, analyze(data), data)[0]['lineage'])

    def test_non_direction_repeat_or_disconnected_raw_does_not_prove_edge(self):
        inputs = input_records([
            'INPUT_DIAG key=102 down=true repeat=true before=0000 after=0008 t_us=10 frame=9',
            'INPUT_DIAG key=90 down=true repeat=false before=0000 after=1000 t_us=11 frame=9',
        ])
        self.assertEqual(direction_edges(inputs, analyze(chain()), chain()), [])
        inputs = input_records([
            'INPUT_DIAG key=102 down=true repeat=false before=0000 after=0008 t_us=10 frame=9',
            'SDOJ_INPUT phase=button_copy t_us=20 frame=10 user=1 buttons=0008',
        ])
        edge = direction_edges(inputs, analyze(chain()), chain())[0]
        self.assertIsNone(edge['raw'])
        self.assertIsNone(edge['lineage'])

    def test_diagnostics_have_no_guest_state_or_sampling_changes(self):
        source = (ROOT / 'games/sdoj/diagnostics/latency_trace.cpp').read_text(encoding='utf-8')
        self.assertIn('REXCVAR_DEFINE_BOOL(sdoj_latency_trace, false,', source)
        self.assertIn('std::try_to_lock', source)
        self.assertIn('lock.unlock();', source)
        self.assertIn('MaxRecords = 131072', source)
        self.assertIn('if (!module_observed.load(std::memory_order_relaxed)) return;', source)
        for forbidden in ('REX_STORE_', 'XamInputGetState(', 'SetFlagByName',
                          'sub_880', 'Sleep(', 'SetThreadPriority'):
            self.assertNotIn(forbidden, source)

    def test_reviewed_sites_match_locally_generated_original_instructions(self):
        # This optional local check reads derived code; none of that code is
        # included in the repository or in the self-authored fixture tests.
        for module in ('CA022100', 'CA022110'):
            source = ROOT / f'generated/sdoj/{module}/sdoj_recomp.0.cpp'
            if not source.exists():
                self.skipTest('Generated game code is not part of a source checkout')
            text = source.read_text(encoding='utf-8')
            sites = {}
            for function in ('88031218', '88034038'):
                body = re.search(r'^DEFINE_REX_FUNC\(sub_' + function + r'\) \{.*?(?=^DEFINE_REX_FUNC|\Z)',
                                 text, re.M | re.S).group()
                instructions = re.findall(r'^\s*// (?!original:)([^\r\n]*)', body, re.M)
                sites.update({int(function, 16) + index * 4: instruction.strip()
                              for index, instruction in enumerate(instructions)})
            for address, expected in {
                0x880340D8: 'li r25,-1', 0x8803417C: 'addi r1,r1,144',
                0x88031288: 'lwz r10,80(r11)',
                0x880313C0: 'lwz r11,' + ('-28200' if module == 'CA022100' else '5888') + '(r31)',
            }.items():
                self.assertEqual(sites[address], expected, (module, hex(address)))


if __name__ == '__main__':
    unittest.main()
