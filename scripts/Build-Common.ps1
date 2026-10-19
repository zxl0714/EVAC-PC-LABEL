#requires -Version 7.0
$ErrorActionPreference = 'Stop'
$CollectionRoot = Split-Path -Parent $PSScriptRoot

function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
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
