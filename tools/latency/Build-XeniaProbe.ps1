#requires -Version 7.0
[CmdletBinding()]
param([string]$SourceDirectory = '.local/input-latency-v080-r1/xenia-source',
      [string]$BuildDirectory = '.local/input-latency-v080-r1/xenia-build')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Set-Location -LiteralPath $root
. (Join-Path $root 'scripts/Build-Common.ps1')
Initialize-CollectionBuildEnvironment
$env:VSLANG = '1033'
[Diagnostics.Process]::GetCurrentProcess().PriorityClass = 'BelowNormal'
$cmake = Get-CollectionTool 'cmake'
$ninja = [IO.Path]::GetFullPath((Get-CollectionTool 'ninja'))
$source = [IO.Path]::GetFullPath($SourceDirectory)
$build = [IO.Path]::GetFullPath($BuildDirectory)
$probeInclude = ([IO.Path]::GetFullPath($PSScriptRoot)).Replace('\', '/')
$rulesOverride = (Join-Path $probeInclude 'msvc_includes.cmake').Replace('\', '/')
$shaderBins = @((Join-Path $root '.local/input-latency-v080-r1/glslang-tools/bin'),
                (Join-Path $root '.local/input-latency-v080-r1/spirv-tools-build/tools'))
foreach ($tool in @((Join-Path $shaderBins[0] 'glslangValidator.exe'),
                    (Join-Path $shaderBins[1] 'spirv-opt.exe'),
                    (Join-Path $shaderBins[1] 'spirv-dis.exe'))) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "Missing shader tool: $tool" }
}
$env:PATH = ($shaderBins -join ';') + ';' + $env:PATH
Remove-Item Env:VULKAN_SDK -ErrorAction SilentlyContinue
Invoke-Checked $cmake @('-S', $source, '-B', $build, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_MAKE_PROGRAM=$ninja",
    '-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl',
    "-DCMAKE_USER_MAKE_RULES_OVERRIDE=$rulesOverride",
    '-DCMAKE_C_FLAGS=/DEVAC_LATENCY_DEPENDENCIES=1',
    "-DCMAKE_CXX_FLAGS=/EHsc /GR /DEVAC_LATENCY_DEPENDENCIES=1 /I`"$probeInclude`"",
    '-DXENIA_BUILD_TESTS=OFF', '-DXENIA_BUILD_MISC=OFF')
Push-Location -LiteralPath $source
try {
    Invoke-Checked 'py' @('-3.12', '-c',
        "import runpy, sys; runpy.run_path('xenia-build.py')['generate_version_h'](sys.argv[1])", $build)
} finally { Pop-Location }
Invoke-Checked $ninja @('-C', $build, '-j', '2',
    'src/xenia/cpu/CMakeFiles/xenia-cpu.dir/processor.cc.obj')
$dependencies = & $ninja -C $build -t deps 'src/xenia/cpu/CMakeFiles/xenia-cpu.dir/processor.cc.obj'
if ($LASTEXITCODE -ne 0 -or -not ($dependencies -match 'ppc_frontend\.h')) {
    throw 'MSVC header dependencies are missing; refusing a partial-ABI measurement build'
}
Invoke-Checked $cmake @('--build', $build, '--target', 'xenia-app', '--parallel', '2')
