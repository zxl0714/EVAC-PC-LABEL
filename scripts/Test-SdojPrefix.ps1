#requires -Version 7.0
[CmdletBinding()]
param()
. (Join-Path $PSScriptRoot 'Build-Common.ps1')
$taskSelf = Get-Process -Id $PID
$taskPriority = $taskSelf.PriorityClass
Push-Location $CollectionRoot
try {
    $taskSelf.PriorityClass = 'BelowNormal'
    Initialize-CollectionBuildEnvironment
    foreach ($taskModule in @('CA022100','CA022110')) {
        $taskFixtureRoot = Join-Path $CollectionRoot ('.local/analysis/sdoj-prefix-fixture/' + $taskModule)
        Invoke-Checked py @('-3.12','scripts/sdoj_prefix_fixture.py','--module',$taskModule,
            '--output',(Join-Path $taskFixtureRoot 'prefix.inc'))
        $taskModuleId = if ($taskModule -eq 'CA022100') { '1' } else { '2' }
        Invoke-Checked (Get-CollectionTool clang++) @('-std=c++23','-O2','-fno-char8_t',
            ('-DMODULE=' + $taskModuleId),'-I.deps/sdk-install/include','-Igames/sdoj',
            ('-I' + $taskFixtureRoot),'tests/sdoj/native_prefix_fixture.cpp',
            '-o',(Join-Path $taskFixtureRoot 'prefix-test.exe'))
        Invoke-Checked (Join-Path $taskFixtureRoot 'prefix-test.exe') @()
    }
} finally {
    Pop-Location
    $taskSelf.PriorityClass = $taskPriority
}
