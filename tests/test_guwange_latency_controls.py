"""Internal observer controls are not a shortcut to first-responsive-image time."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/latency'))
from compare_guwange import check_pair


def fixture():
    def report(pid):
        return dict(capture=dict(pid=pid,complete=True,overflow=0),paired_sources=35,
            requested_sources=35,unpaired_commands=[],statistics={})
    def launch(pid,on):
        return dict(pid=pid,executable_sha256='same',config_sha256='same',stage_sha256='same',
            environment={'EVAC_LATENCY_PIXELS':'pixels'} if on else {})
    return report(1),report(2),launch(1,True),launch(2,False)


class GuwangeControlTests(unittest.TestCase):
    def test_scopes_output_chain_separately(self):
        result=check_pair(*fixture())
        self.assertIn('not first-responsive-image',result['scope'])
        self.assertEqual(result['off_count'],35)

    def test_changed_executable_or_configuration_rejected(self):
        for key in ('executable_sha256','config_sha256','stage_sha256'):
            args=fixture();args[3][key]='changed'
            with self.assertRaises(ValueError):
                check_pair(*args)

    def test_missing_pair_or_wrong_process_rejected(self):
        for mutate in (lambda a:a[1].update(paired_sources=34),
                       lambda a:a[1]['capture'].update(pid=99)):
            args=fixture();mutate(args)
            with self.assertRaises(ValueError):
                check_pair(*args)

    def test_fake_pixel_off_rejected(self):
        args=fixture();args[3]['environment']['EVAC_LATENCY_PIXELS']='still-on'
        with self.assertRaises(ValueError):
            check_pair(*args)


if __name__=='__main__':
    unittest.main()
