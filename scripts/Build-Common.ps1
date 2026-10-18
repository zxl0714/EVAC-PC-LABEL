#requires -Version 7.0
$ErrorActionPreference = 'Stop'
$CollectionRoot = Split-Path -Parent $PSScriptRoot

function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
}

function Get-CollectionTool([string]$Name) {
    $entries = @{clang='llvm/bin/clang.exe'; 'clang++'='llvm/bin/clang++.exe';
        cmake='cmake/bin/cmake.exe'; ninja='ninja/ninja.exe'}
    if (-not $entries.ContainsKey($Name)) { throw "Unknown build tool: $Name" }
    $path = Join-Path $CollectionRoot ('tools/apps/' + $entries[$Name])
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing project-local $Name; run Prepare-Tools.ps1" }
    if ($Name -in @('clang','clang++','cmake','ninja')) {
        $packageId = if ($Name -in @('clang','clang++')) { 'llvm' } else { $Name }
        $lock = Get-Content -LiteralPath (Join-Path $CollectionRoot 'tools/packages.lock.json') -Raw | ConvertFrom-Json
        $entry = $lock.packages | Where-Object id -EQ $packageId
        $expected = $entry.entrySha256
        $receiptPath = Join-Path $CollectionRoot 'tools/state/installed.json'
        if (Test-Path -LiteralPath $receiptPath) {
            $receipt = (Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json).packages | Where-Object id -EQ $packageId
            if ($receipt -and $receipt.archiveSha256 -eq $entry.sha256) { $expected = $receipt.entrySha256 }
        }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $expected) {
            throw "Project tool hash differs: $Name"
        }
    }
    return $path
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
