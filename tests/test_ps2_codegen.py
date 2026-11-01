from pathlib import Path
import json
import sys
import struct
import tempfile
import unittest
import tomllib
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from ps2_codegen import translation_summary, validate_sdk_bindings, validate_sdk_map, validate_static_edges, validate_sdk_lifecycles
import ps2_codegen as codegen

REPORT = '''Functions discovered: 3
Functions processed: 3, recompiled: 2, stubs: 1, skipped: 0, decode failures: 0
Generated functions: 3
Unhandled instructions: 0
Correctness-critical guest fallbacks: 0, failures: 0
Warnings: 1, errors: 0
'''


class ToolEquivalenceTests(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory(prefix='evac-ps2-equivalence-')
        self.addCleanup(temporary.cleanup)
        self.root=Path(temporary.name)
        self.directory=self.root/'.local/module';self.directory.mkdir(parents=True)
        self.tool=self.root/'out/ps2-tools/bin/ps2_recomp.exe';self.tool.parent.mkdir(parents=True)
        self.tool.write_bytes(b'authored changed generator identity')
        self.source=self.directory/'generated';self.source.mkdir()
        self.body='void authored_entry() {}\n'
        (self.source/'entry.cpp').write_text(self.body)
        (self.source/'register_functions.cpp').write_text('void authored_entry();\n')
        self.config=self.directory/'config.toml';self.report=self.directory/'codegen.json'
        self.config.write_text('[general]\ninput="authored-input"\noutput='+json.dumps(self.source.as_posix())+'\n')
        self.previous=dict(format=1,configSha256=codegen.digest(self.config),toolSha256='1'*64,
                           elfSha256='2'*64,functionMapSha256='3'*64,
                           summary=dict(errors=0,decodeFailures=0,unhandledInstructions=0,criticalGuestFallbacks=0))
        self.report.write_text(json.dumps(self.previous))

    def generate(self,config,log,report,root):
        output=Path(tomllib.loads(config.read_text())['general']['output']);output.mkdir()
        (output/'entry.cpp').write_text(self.body)
        (output/'register_functions.cpp').write_text('void authored_entry();\n')
        log.write_text('Authored generation fixture\n')
        record=dict(self.previous,configSha256=codegen.digest(config),toolSha256=codegen.digest(self.tool))
        report.write_text(json.dumps(record))
        return record

    def test_relinked_tool_preserves_original_bodies_timestamps_and_receipts(self):
        originals={p:(p.read_bytes(),p.stat().st_mtime_ns) for p in (*self.source.iterdir(),self.config,self.report)}
        with patch.object(codegen,'generate',side_effect=self.generate) as generated:
            accepted=codegen.refresh_tool(self.config,self.report,self.root)
            again=codegen.verify_equivalence(self.config,self.report,codegen.digest(self.tool),self.root)
            self.assertEqual(accepted,again)
            self.assertEqual(codegen.refresh_tool(self.config,self.report,self.root),accepted)
            self.assertEqual(generated.call_count,1)
        self.assertEqual(originals,{p:(p.read_bytes(),p.stat().st_mtime_ns) for p in originals})

    def test_changed_bodies_require_new_build_inputs(self):
        self.body='void authored_entry() { return; }\n'
        before=(self.source/'entry.cpp').read_bytes()
        with patch.object(codegen,'generate',side_effect=self.generate),self.assertRaisesRegex(ValueError,'bodies changed'):
            codegen.refresh_tool(self.config,self.report,self.root)
        self.assertEqual((self.source/'entry.cpp').read_bytes(),before)
        self.assertFalse(codegen.equivalence_path(self.report,codegen.digest(self.tool)).exists())

    def test_interrupted_comparison_reuses_the_completed_fresh_generation(self):
        directory=self.directory/'codegen-refresh-interrupted';directory.mkdir()
        config=directory/'config.toml';report=directory/'codegen.json'
        text=self.config.read_text().replace(self.source.as_posix(),(directory/'generated').as_posix())
        config.write_text(text)
        self.generate(config,directory/'codegen.log',report,self.root)
        with patch.object(codegen,'generate',side_effect=AssertionError('Generation must be reused')):
            accepted=codegen.refresh_tool(self.config,self.report,self.root)
        self.assertEqual(accepted['sha256'],codegen.tree_identity(self.source)['sha256'])

    def test_stale_source_or_fresh_evidence_cannot_reuse_the_equivalence(self):
        with patch.object(codegen,'generate',side_effect=self.generate):
            codegen.refresh_tool(self.config,self.report,self.root)
        proof=codegen.equivalence_path(self.report,codegen.digest(self.tool))
        evidence=json.loads(proof.read_text())
        body=self.source/'entry.cpp';body.write_text('void changed_authored_entry() {}\n')
        with self.assertRaisesRegex(ValueError,'bodies changed'):
            codegen.verify_equivalence(self.config,self.report,codegen.digest(self.tool),self.root)
        body.write_text(self.body)
        Path(evidence['freshReceipt']).write_text('{}')
        with self.assertRaisesRegex(ValueError,'evidence changed'):
            codegen.verify_equivalence(self.config,self.report,codegen.digest(self.tool),self.root)


class Ps2CodegenTests(unittest.TestCase):
    def test_allocations_cannot_be_native_with_guest_only_release(self):
        for acquire,release in [('sceSifAllocIopHeap','sceSifFreeIopHeap'),
                                ('sceSifAllocSysMemory','sceSifFreeSysMemory')]:
            with self.subTest(acquire=acquire):
                source=f'BindSdkHandler(runtime,0x100000u,"{acquire}")'
                with self.assertRaisesRegex(ValueError,release):
                    validate_sdk_lifecycles({'stubs':[acquire+'@0x100000']},source)
                result=validate_sdk_lifecycles({'stubs':[acquire+'@0x100000',release+'@0x100100']},source)
                self.assertEqual(result,[dict(acquire=acquire,release=release)])

    def test_file_pad_and_callback_lifecycles_are_checked_together(self):
        names=['sceOpen','sceClose','sceMcOpen','sceMcClose','scePadPortOpen','scePadPortClose',
               'CreateSema','DeleteSema','AddIntcHandler','RemoveIntcHandler','AddDmacHandler','RemoveDmacHandler']
        general={'stubs':[name+'@0x100000' for name in names]}
        self.assertEqual(len(validate_sdk_lifecycles(general,'')),6)
        for release in ('sceClose','sceMcClose','scePadPortClose','DeleteSema','RemoveIntcHandler','RemoveDmacHandler'):
            with self.subTest(release=release),self.assertRaisesRegex(ValueError,release):
                validate_sdk_lifecycles({'stubs':[x for x in general['stubs'] if not x.startswith(release+'@')]},'')

    def edge_fixture(self, root, instructions, body, linked=()):
        # Authored ELF32 PT_LOAD bytes: no game executable or analysis cache.
        image=bytearray(0x100+4*len(instructions))
        image[:6]=b'\x7fELF\x01\x01'
        struct.pack_into('<I',image,28,52)
        struct.pack_into('<HH',image,42,32,1)
        struct.pack_into('<8I',image,52,1,0x100,0x100000,0x100000,
                         4*len(instructions),4*len(instructions),5,4)
        for index,opcode in enumerate(instructions):
            struct.pack_into('<I',image,0x100+4*index,opcode)
        elf=root/'fixture.elf';elf.write_bytes(image)
        generated=root/'generated';generated.mkdir()
        (generated/'register_functions.cpp').write_text(''.join(
            f'g_ps2RecompiledFunctionTable[{index}] = entry; // 0x{address:x}\n'
            for index,address in enumerate((0x100000,*linked))))
        (generated/'entry.cpp').write_text('// Address: 0x100000 - 0x100008\n'+body)
        return generated,elf

    def test_missing_reachable_fallthrough_is_rejected_before_build(self):
        # LUI; ADDIU; next block. This is not a return, even without an
        # explicit C++ return statement after the emitted PC assignment.
        for linked in ((),(0x100008,)):
            with self.subTest(linked=linked),tempfile.TemporaryDirectory() as temporary:
                generated,elf=self.edge_fixture(Path(temporary),[0x3c020001,0x24420001,0],
                    'void entry() {ctx->pc = 0x100008u;}\n',linked)
                if linked:
                    result=validate_static_edges(generated,elf)
                    self.assertEqual((result['bodies'],result['reachableTails']),(1,1))
                else:
                    with self.assertRaisesRegex(ValueError,'entry:0x100008'):
                        validate_static_edges(generated,elf)

    def test_unreachable_tail_after_guest_returns_is_not_a_missing_entry(self):
        for instructions in ([0x03e00008,0], [0,0x42000018],
                             [0x08040040,0], [0x10000010,0]):
            with self.subTest(instructions=instructions),tempfile.TemporaryDirectory() as temporary:
                generated,elf=self.edge_fixture(Path(temporary),instructions,
                    'void entry() {ctx->pc = 0x100008u;}\n')
                self.assertEqual(validate_static_edges(generated,elf)['reachableTails'],0)

    def test_conditional_branch_has_a_reachable_false_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            generated,elf=self.edge_fixture(Path(temporary),[0x10400010,0],
                'void entry() {ctx->pc = 0x100008u;}\n')
            with self.assertRaisesRegex(ValueError,'entry:0x100008'):
                validate_static_edges(generated,elf)

    def test_fixed_jump_is_checked_even_when_its_tail_is_unreachable(self):
        with tempfile.TemporaryDirectory() as temporary:
            generated,elf=self.edge_fixture(Path(temporary),[0x08040040,0],
                'void entry() {runtime->dispatchGuestBranch(rdram, ctx, 0x100100u, 0x100000u, 0u);\n'
                'ctx->pc = 0x100008u;}\n')
            with self.assertRaisesRegex(ValueError,'entry:0x100100'):
                validate_static_edges(generated,elf)

    def test_single_instruction_tail_cannot_borrow_a_return_from_previous_function(self):
        with tempfile.TemporaryDirectory() as temporary:
            generated,elf=self.edge_fixture(Path(temporary),[0],
                'void entry() {ctx->pc = 0x100004u;}\n')
            with self.assertRaisesRegex(ValueError,'entry:0x100004'):
                validate_static_edges(generated,elf)

    def test_static_edge_check_rejects_truncated_original_image(self):
        with tempfile.TemporaryDirectory() as temporary:
            generated,elf=self.edge_fixture(Path(temporary),[0],
                'void entry() {ctx->pc = 0x100008u;}\n')
            with self.assertRaisesRegex(ValueError,'no original instruction'):
                validate_static_edges(generated,elf)

    def test_a_selector_does_not_create_a_missing_native_entry(self):
        source='cave::ps2::BindSdkHandler(runtime,0x00100004u,"sceDmaGetChan")'
        gap='Name,Start,End,Size\nentry,0x00100000,0x00100004,4\n'
        with self.assertRaisesRegex(ValueError, 'absent from function map'):
            validate_sdk_map(source,gap)
        self.assertEqual(validate_sdk_map(source,gap+'sdk,0x00100004,0x00100008,4\n'),1)

    def test_native_sdk_bindings_require_generation_selectors(self):
        source='if(!cave::ps2::BindSdkHandler(runtime,0x0010bfd8u,"sceSifAllocIopHeap"))'
        self.assertEqual(validate_sdk_bindings({'stubs':['sceSifAllocIopHeap@0x0010BFD8']},source),1)
        for general in ({},{'stubs':['sceSifAllocIopHeap@0x0010bfd0']},
                        {'stubs':['other@0x0010bfd8']},
                        {'stubs':['other@0x0010bfd8','sceSifAllocIopHeap@0x0010bfd8']}):
            with self.subTest(general=general),self.assertRaises(ValueError):
                validate_sdk_bindings(general,source)
        with self.assertRaises(ValueError):validate_sdk_bindings({},'// no declared bindings')

    def test_complete_report_and_crlf_preserve_warning_count(self):
        result = translation_summary(REPORT.replace('\n', '\r\n'))
        self.assertEqual((result['functions'], result['stubs'], result['warnings']), (3, 1, 1))

    def test_zero_process_exit_is_not_proof_of_valid_translation(self):
        failed = REPORT.replace('Unhandled instructions: 0', 'Unhandled instructions: 16760')
        failed = failed.replace('errors: 0', 'errors: 16760')
        failed += 'Recompilation completed successfully\n'
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            translation_summary(failed)

    def test_error_kinds_and_incomplete_output_reject_acceptance(self):
        for before, after in [('errors: 0', 'errors: 1'), ('failures: 0', 'failures: 1'),
                              ('skipped: 0', 'skipped: 1'), ('decode failures: 0', 'decode failures: 1'),
                              ('Generated functions: 3', 'Generated functions: 2'),
                              ('Functions discovered: 3', 'Functions discovered: 0')]:
            with self.subTest(after=after), self.assertRaises(ValueError):
                translation_summary(REPORT.replace(before, after))

    def test_missing_or_ambiguous_report_fails_closed(self):
        for text in ('Recompilation completed successfully', REPORT+REPORT,
                     REPORT.replace('Warnings: 1, errors: 0\n', '')):
            with self.assertRaises(ValueError):
                translation_summary(text)


if __name__ == '__main__':
    unittest.main()
