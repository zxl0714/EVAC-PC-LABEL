from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts/ps4'))
import ps4_elf_fixtures as fixtures
import prepare_native


class ElfPreparationOracleTests(unittest.TestCase):
    def test_authored_fixture_contracts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            for name,raw,module,contract,valid in fixtures.cases():
                with self.subTest(case=name):
                    source=root/(name+'.elf'); source.write_bytes(raw)
                    libc=root/(name+'.libc')
                    if contract is not None: libc.write_bytes(contract)
                    if valid:
                        data,report=prepare_native.prepare(source,{},libc if contract is not None else None,module)
                        self.assertEqual(data[:8],b'KTMODULE' if module else b'KTPROBE1')
                        self.assertEqual(report['probe_sha256'],prepare_native.hashlib.sha256(data).hexdigest())
                        self.assertEqual(data[64:96],prepare_native.hashlib.sha256(raw).digest())
                    else:
                        with self.assertRaises((ValueError,KeyError,IndexError)):
                            prepare_native.prepare(source,{},libc if contract is not None else None,module)


if __name__=='__main__': unittest.main()
