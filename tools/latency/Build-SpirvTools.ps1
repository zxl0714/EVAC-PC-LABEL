#requires -Version 7.0
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Set-Location -LiteralPath $root
. (Join-Path $root 'scripts/Build-Common.ps1')
Initialize-CollectionBuildEnvironment
[Diagnostics.Process]::GetCurrentProcess().PriorityClass = 'BelowNormal'
$cmake = Get-CollectionTool 'cmake'
$compiler = [IO.Path]::GetFullPath((Get-CollectionTool 'clang-cl'))
$ninja = [IO.Path]::GetFullPath((Get-CollectionTool 'ninja'))
$source = Join-Path $root '.local/input-latency-v080-r1/xenia-source/third_party/SPIRV-Tools'
$build = Join-Path $root '.local/input-latency-v080-r1/spirv-tools-build'
$headers = (Join-Path $root '.local/input-latency-v080-r1/spirv-headers').Replace('\', '/')
Invoke-Checked $cmake @('-S', $source, '-B', $build, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_C_COMPILER=$compiler", "-DCMAKE_CXX_COMPILER=$compiler",
    "-DSPIRV-Headers_SOURCE_DIR=$headers", '-DSPIRV_SKIP_TESTS=ON',
    '-DSPIRV_TOOLS_USE_MIMALLOC=OFF', '-DSPIRV_TOOLS_BUILD_STATIC=ON',
    '-DBUILD_SHARED_LIBS=OFF')
Invoke-Checked $cmake @('--build', $build, '--target', 'spirv-opt', 'spirv-dis', '--parallel', '2')
