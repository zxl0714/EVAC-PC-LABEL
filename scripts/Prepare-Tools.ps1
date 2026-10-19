#requires -Version 7.0
[CmdletBinding()]
param([string]$CacheFrom, [switch]$Ps4)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$arguments = @('-3.12', (Join-Path $PSScriptRoot 'prepare_tools.py'))
if ($CacheFrom) { $arguments += @('--cache-from', [IO.Path]::GetFullPath($CacheFrom)) }
& py @arguments
if ($LASTEXITCODE -ne 0) { throw 'Tool preparation failed; existing files were preserved' }
if ($Ps4) {
    $ps4PythonRoot = Join-Path $projectRoot 'tools/apps/ps4-python'
    $ps4Python = Join-Path $ps4PythonRoot 'Scripts/python.exe'
    for ($ps4Ancestor=$ps4PythonRoot; $ps4Ancestor; $ps4Ancestor=Split-Path -Parent $ps4Ancestor) {
        if ((Test-Path -LiteralPath $ps4Ancestor) -and
            ((Get-Item -LiteralPath $ps4Ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'PS4 tool output cannot use a linked directory'
        }
        if ($ps4Ancestor -eq $projectRoot) { break }
    }
    if (-not (Test-Path -LiteralPath $ps4Python)) {
        if (Test-Path -LiteralPath $ps4PythonRoot) { throw 'Incomplete PS4 Python environment; existing files preserved' }
        & py -3.12 -m venv $ps4PythonRoot
        if ($LASTEXITCODE -ne 0) { throw 'Cannot create local PS4 tool Python environment' }
    }
    $ps4PythonPrefix = & $ps4Python -c 'import sys; print(sys.prefix)'
    if ($LASTEXITCODE -ne 0 -or [IO.Path]::GetFullPath($ps4PythonPrefix) -ne $ps4PythonRoot) {
        throw 'PS4 Python environment belongs to a different path'
    }
    $ps4PythonLock = Join-Path $projectRoot 'tools/python-ps4.lock.txt'
    $ps4WheelCache = Join-Path $projectRoot 'tools/downloads/python-ps4'
    New-Item -ItemType Directory -Path $ps4WheelCache -Force | Out-Null
    & $ps4Python -m pip download --disable-pip-version-check --require-hashes --only-binary=:all: --no-deps -r $ps4PythonLock -d $ps4WheelCache
    if ($LASTEXITCODE -ne 0) { throw 'Cannot download the pinned zstd decoder' }
    & $ps4Python -m pip install --disable-pip-version-check --require-hashes --only-binary=:all: --no-deps --no-index --find-links $ps4WheelCache -r $ps4PythonLock
    if ($LASTEXITCODE -ne 0) { throw 'Cannot install the pinned local zstd decoder' }
    $ps4Arguments = @((Join-Path $PSScriptRoot 'prepare_tools.py'), '--ps4')
    if ($CacheFrom) { $ps4Arguments += @('--cache-from', [IO.Path]::GetFullPath($CacheFrom)) }
    & $ps4Python @ps4Arguments
    if ($LASTEXITCODE -ne 0) { throw 'PS4 tool preparation failed; existing files preserved' }
}
