#requires -Version 7.0
[CmdletBinding()]
param([ValidateSet('Source','Native','All')][string]$Scope='All')
$ErrorActionPreference = 'Stop'
$collectionTestRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $collectionTestRoot
try {
    if ($Scope -in @('Source','All')) {
        & py -3.12 -m unittest discover -s tests -v
        if ($LASTEXITCODE -ne 0) { throw 'Source tests failed' }
        & py -3.12 scripts/project.py audit
        if ($LASTEXITCODE -ne 0) { throw 'Source audit failed' }
    }
    if ($Scope -in @('Native','All')) {
        $ctest = Join-Path $collectionTestRoot 'tools/apps/cmake/bin/ctest.exe'
        if (-not (Test-Path -LiteralPath $ctest)) { throw 'Run Prepare-Tools.ps1 and Build.ps1 first' }
        & $ctest --test-dir out/common --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'Native tests failed' }
    }
} finally { Pop-Location }
