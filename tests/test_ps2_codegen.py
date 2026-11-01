from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from ps2_codegen import translation_summary, validate_sdk_bindings, validate_sdk_map

REPORT = '''Functions discovered: 3
Functions processed: 3, recompiled: 2, stubs: 1, skipped: 0, decode failures: 0
Generated functions: 3
Unhandled instructions: 0
Correctness-critical guest fallbacks: 0, failures: 0
Warnings: 1, errors: 0
'''


class Ps2CodegenTests(unittest.TestCase):
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
