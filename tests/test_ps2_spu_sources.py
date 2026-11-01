import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from ps2_spu_sources import prepare,strip_state_methods,STATE_METHODS


class SpuSourcesTests(unittest.TestCase):
    def test_state_removal_keeps_neighboring_dsp_implementation(self):
        dsp='int DecodeSample(int sample) { return (sample * 17) >> 3; }\n'
        methods=''.join(f'void {name}(Archive& value)\n{{\n\tvalue.Save();\n}}\n' for name in STATE_METHODS)
        self.assertEqual(strip_state_methods(dsp+methods+dsp),dsp+dsp)
        with self.assertRaises(ValueError):strip_state_methods(methods.replace('SaveState','OtherState',1))

    def test_all_pins_validate_before_staging_and_inputs_remain_immutable(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);deps=root/'deps';deps.mkdir();output=root/'out'
            source=b'int DecodeSample(int sample) { return sample * 2; }\n'
            (deps/'dsp.cpp').write_bytes(source)
            (deps/'LICENSE.txt').write_bytes(b'authored license fixture\n')
            files=[dict(source=name,target=name,sha256=hashlib.sha256((deps/name).read_bytes()).hexdigest())
                   for name in ('dsp.cpp','LICENSE.txt')]
            inventory=root/'sources.json';inventory.write_text(json.dumps({'files':files}))
            result=prepare(deps,output,inventory)
            self.assertEqual(result['files'],2)
            self.assertEqual((output/'dsp.cpp').read_bytes(),source)
            self.assertEqual((deps/'dsp.cpp').read_bytes(),source)
            (deps/'LICENSE.txt').write_bytes(b'changed input')
            with self.assertRaises(ValueError):prepare(deps,root/'bad-out',inventory)
            self.assertFalse((root/'bad-out').exists())


if __name__=='__main__':unittest.main()
