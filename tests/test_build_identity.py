from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from build_receipt import signature,dependency_ids


class BuildIdentityTests(unittest.TestCase):
    def test_pe_and_pdb_ids_match(self):
        expected={'guid':'12345678-1234-5678-9abc-0123456789ab','age':2}
        pe='PDBGUID: {12345678-1234-5678-9ABC-0123456789AB}\nPDBAge: 2\n'
        pdb='GUID: {12345678-1234-5678-9abc-0123456789ab}\nAge: 2\n'
        self.assertEqual(signature(pe),expected)
        self.assertEqual(signature(pdb,True),expected)

    def test_missing_identity_is_rejected(self):
        for text in ('','PDBAge: 1','PDBGUID: {not-a-guid}\nPDBAge: 1'):
            with self.subTest(text=text),self.assertRaises(ValueError):signature(text)

    def test_age_mismatch_is_visible(self):
        left=signature('PDBGUID: {12345678-1234-5678-9abc-0123456789ab}\nPDBAge: 1')
        right=signature('GUID: {12345678-1234-5678-9abc-0123456789ab}\nAge: 2',True)
        self.assertNotEqual(left,right)

    def test_describe_text_is_not_dependency_identity(self):
        prefix=' '+'1'*40+' third_party/example'
        self.assertEqual(dependency_ids([prefix+' (tag-a)']),dependency_ids([prefix+' (different-tags)']))

    def test_dependency_mismatch_is_rejected(self):
        for prefix in ('+','-','U'):
            with self.subTest(prefix=prefix),self.assertRaises(ValueError):
                dependency_ids([prefix+'1'*40+' third_party/example'])


if __name__=='__main__':unittest.main()
