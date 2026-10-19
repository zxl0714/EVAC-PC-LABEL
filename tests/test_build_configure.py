"""Exercise the actual PowerShell pre-build guard with authored CMake caches."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PWSH = shutil.which('pwsh')


@unittest.skipUnless(PWSH, 'PowerShell 7 is needed for build entry-point checks')
class BuildConfigureTests(unittest.TestCase):
    def exercise(self, *, mismatch='', missing='', first_reset=False, configure_failure=False):
        with tempfile.TemporaryDirectory(prefix='cave configure ') as temporary:
            build = Path(temporary)
            # Mock only process execution, not the configure wrapper or its guard.
            script = r'''
param($Common, $Build, $Mismatch, $Missing, $FirstReset, $ConfigureFailure)
. $Common
$script:passes = 0
function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    $script:passes++
    if ($ConfigureFailure -eq 'yes') { throw 'authored configure failure' }
    $type = if ($FirstReset -eq 'yes' -and $script:passes -eq 1) { 'Debug' } else { 'Release' }
    $values = [ordered]@{
        CMAKE_BUILD_TYPE = $type
        CMAKE_CXX_FLAGS_RELEASE = '-O3 -DNDEBUG -g -gcodeview -mssse3 -msse4.1'
        CMAKE_CXX_COMPILER = ($Build + '/compiler.exe')
        CMAKE_MAKE_PROGRAM = ($Build + '/ninja.exe')
        CMAKE_INSTALL_PREFIX = ($Build + '/install')
        CMAKE_GENERATOR = 'Ninja'
        CMAKE_HOME_DIRECTORY = $Build
        FEATURE_ENABLED = 'OFF'
    }
    if ($Mismatch) { $values[$Mismatch] = 'wrong' }
    $lines = foreach ($key in $values.Keys) {
        if ($key -ne $Missing) { $key + ':STRING=' + $values[$key] }
    }
    [IO.File]::WriteAllLines((Join-Path $Build 'CMakeCache.txt'), $lines)
}
$arguments = @('-S', $Build, '-B', $Build, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG -g -gcodeview -mssse3 -msse4.1',
    ('-DCMAKE_CXX_COMPILER=' + (Join-Path $Build 'compiler.exe')),
    ('-DCMAKE_MAKE_PROGRAM=' + (Join-Path $Build 'ninja.exe')),
    ('-DCMAKE_INSTALL_PREFIX=' + (Join-Path $Build 'install')),
    '-DFEATURE_ENABLED:BOOL=OFF')
$accepted = $false
$message = ''
try {
    Invoke-CollectionConfigure 'authored-cmake' $arguments $Build
    $accepted = $true
} catch { $message = $_.Exception.Message }
@{accepted=$accepted; passes=$script:passes; message=$message} | ConvertTo-Json -Compress
'''
            harness = build / 'harness.ps1'
            harness.write_text(script, encoding='utf-8')
            result = subprocess.run([PWSH, '-NoProfile', '-NonInteractive', '-File', str(harness),
                                     str(ROOT / 'scripts/Build-Common.ps1'), str(build),
                                     mismatch, missing, 'yes' if first_reset else 'no',
                                     'yes' if configure_failure else 'no'],
                                    capture_output=True, text=True, encoding='utf-8', timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return json.loads(result.stdout)

    def test_compiler_reset_is_reapplied_and_path_separators_are_normalized(self):
        result = self.exercise(first_reset=True)
        self.assertTrue(result['accepted'], result)
        self.assertEqual(result['passes'], 2)

    def test_retained_wrong_build_type_flags_or_features_cannot_start_build(self):
        for key in ('CMAKE_BUILD_TYPE', 'CMAKE_CXX_FLAGS_RELEASE', 'FEATURE_ENABLED', 'CMAKE_GENERATOR',
                    'CMAKE_CXX_COMPILER', 'CMAKE_MAKE_PROGRAM'):
            with self.subTest(key=key):
                result = self.exercise(mismatch=key)
                self.assertFalse(result['accepted'], result)
                self.assertIn(key, result['message'])
                self.assertEqual(result['passes'], 2)

    def test_missing_requested_compiler_is_rejected(self):
        result = self.exercise(missing='CMAKE_CXX_COMPILER')
        self.assertFalse(result['accepted'], result)
        self.assertIn('missing requested setting CMAKE_CXX_COMPILER', result['message'])

    def test_configure_error_stops_without_retrying_or_accepting_old_cache(self):
        result = self.exercise(configure_failure=True)
        self.assertFalse(result['accepted'], result)
        self.assertEqual(result['passes'], 1)
        self.assertIn('authored configure failure', result['message'])

    def test_all_native_build_entry_points_use_the_same_guard(self):
        for name in ('Build.ps1', 'Build-Sdk.ps1', 'Build-Ps4.ps1'):
            text = (ROOT / 'scripts' / name).read_text(encoding='utf-8')
            self.assertIn('Invoke-CollectionConfigure ', text, name)


if __name__ == '__main__':
    unittest.main()
