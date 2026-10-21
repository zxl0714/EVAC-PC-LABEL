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
    $callbackRoot = Join-Path $CollectionRoot '.local/analysis/g2-callback-fixture'
    Invoke-Checked py @('-3.12','scripts/g2_callback_fixture.py','--output',
        (Join-Path $callbackRoot 'callback.inc'))
    Invoke-Checked (Get-CollectionTool clang++) @('-std=c++23','-O2','-fno-char8_t',
        '-I.deps/sdk-install/include',('-I' + $callbackRoot),
        'tests/espgaluda2/native_callback_fixture.cpp','-o',(Join-Path $callbackRoot 'callback-test.exe'))
    Invoke-Checked (Join-Path $callbackRoot 'callback-test.exe') @()
} finally {
    Pop-Location
    $callbackSelf.PriorityClass = $callbackPriority
}
