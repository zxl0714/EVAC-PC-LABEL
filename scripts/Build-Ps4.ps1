#requires -Version 7.0
[CmdletBinding()]
param([ValidateRange(1,8)][int]$Jobs=2)
. (Join-Path $PSScriptRoot 'Build-Common.ps1')
$ps4BuildProcess = Get-Process -Id $PID
$ps4PreviousPriority = $ps4BuildProcess.PriorityClass
try {
    $ps4BuildProcess.PriorityClass = 'BelowNormal'
    Initialize-CollectionBuildEnvironment
    $ps4Compiler = Join-Path $CollectionRoot 'tools/apps/llvm-ps4/bin/clang-cl.exe'
    if (-not (Test-Path -LiteralPath $ps4Compiler)) { throw 'Run Prepare-Tools.ps1 -Ps4 first' }
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
        foreach ($ps4Pass in 1..2) { Invoke-Checked $ps4CMake $ps4Configure }
        $ps4Cache = @{}
        foreach ($line in Get-Content -LiteralPath (Join-Path $ps4Build 'CMakeCache.txt')) {
            if ($line -match '^([^/#][^:]*):[^=]+=(.*)$') { $ps4Cache[$Matches[1]]=$Matches[2] }
        }
        if ($ps4Cache['CMAKE_BUILD_TYPE'] -ne 'RelWithDebInfo' -or
            $ps4Cache['CMAKE_CXX_FLAGS_RELWITHDEBINFO'] -ne '/O2 /Ob1 /DNDEBUG' -or
            $ps4Cache['CMAKE_C_FLAGS_RELWITHDEBINFO'] -ne '/O2 /Ob1 /DNDEBUG' -or
            [IO.Path]::GetFullPath($ps4Cache['CMAKE_CXX_COMPILER']) -ne $ps4Compiler -or
            [IO.Path]::GetFullPath($ps4Cache['CMAKE_MAKE_PROGRAM']) -ne $ps4Ninja) {
            throw 'PS4 CMake retained a different compiler, build type or Ninja; existing outputs preserved'
        }
        Invoke-Checked $ps4CMake @('--build',$ps4Build,'--parallel',"$Jobs")
    }
    Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'ps4_build_receipt.py'),'finish','--state',$ps4State)
    Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'stage_ps4.py'))
    Write-Host 'PS4 native targets built. No ROM was required and no game was launched.'
} finally { $ps4BuildProcess.PriorityClass = $ps4PreviousPriority }
