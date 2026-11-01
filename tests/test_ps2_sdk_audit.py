from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from ps2_sdk_audit import audit


class SdkAuditTests(unittest.TestCase):
    def fixture(self,root,address):
        generated=root/'generated';generated.mkdir()
        (generated/'register_functions.cpp').write_text(
            'extern const uint32_t g_ps2RecompiledFunctionTableBase = 0x100000u;\n'
            'extern const uint32_t g_ps2RecompiledFunctionTableEnd = 0x100010u;\n'
            'g_ps2RecompiledFunctionTable[0] = original; // 0x100000\n')
        (generated/'original.cpp').write_text('void original() {}\n')
        bindings=root/'bindings.cpp'
        bindings.write_text(f'BindSdkHandler(runtime,0x{address:x}u,"ExampleSdk")')
        config=root/'game.toml'
        config.write_text('[general]\noutput = "'+generated.as_posix()+'"\n'
                          'sdk_bindings = "'+bindings.as_posix()+'"\n'
                          f'stubs = ["ExampleSdk@0x{address:x}"]\n')
        analysis=root/'analysis.toml';analysis.write_text('[general]\n')
        return config,analysis

    def test_runtime_registration_fills_an_initially_empty_dense_slot(self):
        with tempfile.TemporaryDirectory() as temporary:
            config,analysis=self.fixture(Path(temporary),0x100008)
            result=audit(config,analysis)
            self.assertEqual(result['selectedRouteMismatches'],[])
            self.assertEqual(result['entries'][0]['status'],'native_runtime')

    def test_out_of_range_binding_is_not_treated_as_a_registered_route(self):
        with tempfile.TemporaryDirectory() as temporary:
            config,analysis=self.fixture(Path(temporary),0x200000)
            result=audit(config,analysis)
            self.assertEqual(len(result['selectedRouteMismatches']),1)
            self.assertEqual(result['entries'][0]['status'],'not_compiled')

    def test_retained_guest_rpc_client_remains_visible_for_semantic_review(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);config,analysis=self.fixture(root,0x100008)
            analysis.write_text('[general]\nentry_points = ["ClientHelper@0x100000", "sceSifCallRpc@0x10000c"]\n')
            table=root/'generated/register_functions.cpp'
            table.write_text(table.read_text()+'g_ps2RecompiledFunctionTable[3] = rpc; // 0x10000c\n')
            (root/'generated/rpc.cpp').write_text('void rpc(){ps2_stubs::sceSifCallRpc();}\n')
            (root/'generated/original.cpp').write_text(
                'void original(){runtime->dispatchGuestBranch(rdram, ctx, 0x10000cu, 0u);}\n')
            result=audit(config,analysis)
            self.assertEqual(len(result['retainedRpcClients']),1)
            self.assertEqual(result['retainedRpcClients'][0]['names'],['ClientHelper'])


if __name__=='__main__':
    unittest.main()
