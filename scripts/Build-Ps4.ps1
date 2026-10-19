#requires -Version 7.0
[CmdletBinding()]
param([ValidateRange(1,8)][int]$Jobs=2)
. (Join-Path $PSScriptRoot 'Build-Common.ps1')
$ps4BuildProcess = Get-Process -Id $PID
$ps4PreviousPriority = $ps4BuildProcess.PriorityClass
try {
    $ps4BuildProcess.PriorityClass = 'BelowNormal'
    Initialize-CollectionBuildEnvironment
    $ps4Compiler = Get-CollectionTool 'clang-cl'
    $ps4State = Join-Path $CollectionRoot ('.local/build-inputs/ps4-' + [guid]::NewGuid().ToString('N') + '.json')
    Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'ps4_build_receipt.py'),'begin','--state',$ps4State)
    $ps4CMake = Get-CollectionTool 'cmake'
    $ps4Ninja = Get-CollectionTool 'ninja'
    foreach ($ps4Part in @(
        @{Source='third_party/ps4/gpu_core';Build='out/ps4-gpu'},
        @{Source='backends/ps4';Build='out/ps4-native'}
    )) {
        $ps4Source = Join-Path $CollectionRoot $ps4Part.Source
        $ps4Build = Join-Path $CollectionRoot $ps4Part.Build
        $ps4Configure = @('-S',$ps4Source,'-B',$ps4Build,'-G','Ninja','-DCMAKE_BUILD_TYPE=RelWithDebInfo',
            "-DCMAKE_CXX_COMPILER=$ps4Compiler","-DCMAKE_C_COMPILER=$ps4Compiler","-DCMAKE_MAKE_PROGRAM=$ps4Ninja",
            '-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=/O2 /Ob1 /DNDEBUG',
            '-DCMAKE_C_FLAGS_RELWITHDEBINFO=/O2 /Ob1 /DNDEBUG',
            '-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=ProgramDatabase')
        Invoke-CollectionConfigure $ps4CMake $ps4Configure $ps4Build
        Invoke-Checked $ps4CMake @('--build',$ps4Build,'--parallel',"$Jobs")
    }
    Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'ps4_build_receipt.py'),'finish','--state',$ps4State)
    Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'stage_ps4.py'))
    Write-Host 'PS4 native targets built. No ROM was required and no game was launched.'
} finally { $ps4BuildProcess.PriorityClass = $ps4PreviousPriority }
