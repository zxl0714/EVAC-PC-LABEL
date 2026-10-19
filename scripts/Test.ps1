#requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Source','Native','All')][string]$Scope='All',
    [ValidateSet('espgaluda2','futari','ketsui','sdoj')][string[]]$Games=@()
)
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
        $xboxGames = @($Games | Where-Object { $_ -ne 'ketsui' })
        $builds=if (-not $xboxGames.Count) { @('out/common') } else {
            $selected=@($xboxGames | Sort-Object -Unique)
            $name=if ($selected.Count -gt 1) { 'xbox360' } else { $selected[0] }
            if (Test-Path -LiteralPath (Join-Path $collectionTestRoot "out/$name/CMakeCache.txt")) {
                @("out/$name")
            } else { @("out/$name-native") }
        }
        foreach ($build in $builds) {
            & $ctest --test-dir $build --output-on-failure
            if ($LASTEXITCODE -ne 0) { throw "Native tests failed: $build" }
        }
        if ('ketsui' -in $Games) {
            & $ctest --test-dir (Join-Path $collectionTestRoot 'out/ps4-gpu') --output-on-failure
            if ($LASTEXITCODE -ne 0) { throw 'PS4 GPU tests failed' }
            & py -3.12 (Join-Path $PSScriptRoot 'ps4/run_native_checks.py')
            if ($LASTEXITCODE -ne 0) { throw 'PS4 native checks failed' }
            & py -3.12 (Join-Path $PSScriptRoot 'ps4/run_prepare_checks.py')
            if ($LASTEXITCODE -ne 0) { throw 'PS4 native ELF preparation checks failed' }
            & py -3.12 (Join-Path $PSScriptRoot 'ps4/run_resource_checks.py')
            if ($LASTEXITCODE -ne 0) { throw 'PS4 native resource preparation checks failed' }
            & py -3.12 (Join-Path $PSScriptRoot 'ps4/run_cache_checks.py')
            if ($LASTEXITCODE -ne 0) { throw 'PS4 native prepared-cache checks failed' }
        }
    }
} finally { Pop-Location }
