#requires -Version 7.0
$ErrorActionPreference = 'Stop'
$CollectionRoot = Split-Path -Parent $PSScriptRoot

function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
}

function Invoke-CollectionConfigure([string]$Command, [string[]]$Arguments, [string]$BuildDirectory) {
    # A compiler change makes CMake discard the old cache and reconfigure with
    # defaults, including lost -D options. Reapply our inputs before any build.
    foreach ($pass in 1..2) { Invoke-Checked $Command $Arguments }
    $cachePath = Join-Path $BuildDirectory 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
        throw "CMake did not produce its configuration cache: $cachePath"
    }
    $cache = @{}
    foreach ($line in Get-Content -LiteralPath $cachePath) {
        if ($line -match '^([^/#][^:]*):[^=]+=(.*)$') { $cache[$Matches[1]] = $Matches[2] }
    }
    $expected = @{}
    for ($index = 0; $index -lt $Arguments.Count; $index++) {
        $argument = $Arguments[$index]
        if ($argument -match '^-D([^:=]+)(?::[^=]+)?=(.*)$') {
            $expected[$Matches[1]] = $Matches[2]
        } elseif ($argument -eq '-G') {
            $expected['CMAKE_GENERATOR'] = $Arguments[$index + 1]
        } elseif ($argument -eq '-S') {
            $expected['CMAKE_HOME_DIRECTORY'] = $Arguments[$index + 1]
        }
    }
    $pathKeys = @('CMAKE_C_COMPILER', 'CMAKE_CXX_COMPILER', 'CMAKE_MAKE_PROGRAM',
        'CMAKE_INSTALL_PREFIX', 'CMAKE_PREFIX_PATH', 'CMAKE_HOME_DIRECTORY')
    foreach ($key in $expected.Keys) {
        if (-not $cache.ContainsKey($key)) { throw "CMake cache is missing requested setting $key; build not started" }
        $wanted = [string]$expected[$key]
        $actual = [string]$cache[$key]
        $comparison = [StringComparison]::Ordinal
        if ($key -in $pathKeys) {
            $wanted = (($wanted -split ';' | ForEach-Object { [IO.Path]::GetFullPath($_).TrimEnd([char]92, [char]47) }) -join ';')
            $actual = (($actual -split ';' | ForEach-Object { [IO.Path]::GetFullPath($_).TrimEnd([char]92, [char]47) }) -join ';')
            if ($IsWindows) { $comparison = [StringComparison]::OrdinalIgnoreCase }
        }
        if (-not [string]::Equals($actual, $wanted, $comparison)) {
            throw "CMake retained $key='$actual', expected '$wanted'; build not started"
        }
    }
}

function Get-CollectionTool([string]$Name) {
    $path = & py -3.12 (Join-Path $PSScriptRoot 'toolchain.py') path $Name --root $CollectionRoot
    if ($LASTEXITCODE -ne 0 -or -not $path) { throw "Missing or changed project-local $Name; run Prepare-Tools.ps1" }
    return [string]$path
}

function Initialize-CollectionBuildEnvironment {
    $locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $locator)) { throw 'Install VS 2022 C++ Build Tools and Windows SDK' }
    $installation = @(& $locator -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    if (-not $installation.Count) { throw 'Visual Studio 2022 x64 C++ tools not found' }
    $setup = Join-Path $installation[0] 'VC/Auxiliary/Build/vcvars64.bat'
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $env:ComSpec
    $info.Arguments = '/d /c call "' + $setup + '" >nul && set'
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($info)
    try {
        $output = $process.StandardOutput.ReadToEndAsync()
        $errors = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(60000)) { throw 'VS environment setup did not finish' }
        if ($process.ExitCode -ne 0) { throw $errors.GetAwaiter().GetResult() }
        foreach ($line in ($output.GetAwaiter().GetResult() -split '\r?\n')) {
            $split = $line.IndexOf('=')
            if ($split -gt 0) {
                [Environment]::SetEnvironmentVariable($line.Substring(0, $split), $line.Substring($split + 1), 'Process')
            }
        }
    } finally { $process.Dispose() }
    foreach ($tool in @('clang','clang++','cmake','ninja')) { [void](Get-CollectionTool $tool) }
}
