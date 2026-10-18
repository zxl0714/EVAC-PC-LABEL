#requires -Version 7.0
[CmdletBinding()]
param([ValidateRange(1,8)][int]$Jobs=2)
. (Join-Path $PSScriptRoot 'Build-Common.ps1')
$taskProcess = Get-Process -Id $PID
$previousPriority = $taskProcess.PriorityClass
try {
    $taskProcess.PriorityClass = 'BelowNormal'
    Initialize-CollectionBuildEnvironment
    $cmake = Get-CollectionTool 'cmake'
    $build = Join-Path $CollectionRoot 'out/common'
    Invoke-Checked $cmake @('-S',$CollectionRoot,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
        ('-DCMAKE_CXX_COMPILER=' + (Get-CollectionTool 'clang++')),
        ('-DCMAKE_MAKE_PROGRAM=' + (Get-CollectionTool 'ninja')),
        '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG -g -gcodeview',
        '-DCMAKE_EXE_LINKER_FLAGS=-Xlinker /DEBUG:FULL -Xlinker /OPT:REF -Xlinker /OPT:ICF')
    Invoke-Checked $cmake @('--build',$build,'--parallel',"$Jobs")
    Write-Host "Built catalogue/inspection tools under $build/bin. Game migration and launcher are next."
} finally { $taskProcess.PriorityClass = $previousPriority }
