#requires -Version 7.0
[CmdletBinding()]
param()
. (Join-Path $PSScriptRoot 'Build-Common.ps1')
$callbackSelf = Get-Process -Id $PID
$callbackPriority = $callbackSelf.PriorityClass
Push-Location $CollectionRoot
try {
    $callbackSelf.PriorityClass = 'BelowNormal'
    Initialize-CollectionBuildEnvironment
    foreach ($callbackModule in @('dat00','dat11','dat12','dat15','dat162','dat163','dat164')) {
        $callbackRoot = Join-Path $CollectionRoot ('.local/analysis/g2-callback-fixture/' + $callbackModule)
        Invoke-Checked py @('-3.12','scripts/g2_callback_fixture.py','--module',$callbackModule,'--output',
            (Join-Path $callbackRoot 'callback.inc'))
        Invoke-Checked (Get-CollectionTool clang++) @('-std=c++23','-O2','-fno-char8_t',
            '-I.deps/sdk-install/include',('-I' + $callbackRoot),
            'tests/espgaluda2/native_callback_fixture.cpp','-o',(Join-Path $callbackRoot 'callback-test.exe'))
        Invoke-Checked (Join-Path $callbackRoot 'callback-test.exe') @()
    }
} finally {
    Pop-Location
    $callbackSelf.PriorityClass = $callbackPriority
}
