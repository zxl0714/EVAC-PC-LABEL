#requires -Version 7.0
[CmdletBinding()]
param([ValidateRange(1,8)][int]$Jobs=2)
. (Join-Path $PSScriptRoot 'Build-Common.ps1')
$launcherCheckProcess = Get-Process -Id $PID
$launcherCheckPriority = $launcherCheckProcess.PriorityClass
try {
    $launcherCheckProcess.PriorityClass = 'BelowNormal'
    Initialize-CollectionBuildEnvironment
    $launcherCheckBuild = Join-Path $CollectionRoot 'out/launcher-checks'
    $launcherCheckCmake = Get-CollectionTool 'cmake'
    $launcherCheckArgs = @('-S',(Join-Path $CollectionRoot 'tests/launcher'),'-B',$launcherCheckBuild,
        '-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
        ('-DCMAKE_CXX_COMPILER=' + (Get-CollectionTool 'clang++')),
        ('-DCMAKE_MAKE_PROGRAM=' + (Get-CollectionTool 'ninja')))
    Invoke-CollectionConfigure $launcherCheckCmake $launcherCheckArgs $launcherCheckBuild
    Invoke-Checked $launcherCheckCmake @('--build',$launcherCheckBuild,'--parallel',"$Jobs")
    $launcherCheckCtest = Join-Path (Split-Path -Parent $launcherCheckCmake) 'ctest.exe'
    Invoke-Checked $launcherCheckCtest @('--test-dir',$launcherCheckBuild,'--output-on-failure','-V')
} finally { $launcherCheckProcess.PriorityClass = $launcherCheckPriority }
