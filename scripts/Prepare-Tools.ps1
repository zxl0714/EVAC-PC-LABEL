#requires -Version 7.0
[CmdletBinding()]
param([string]$CacheFrom, [switch]$Ps4)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
# Both backends use the same archive and local decoder. -Ps4 is a legacy alias.
$toolPythonRoot = Join-Path $projectRoot 'tools/apps/tool-python'
$toolPython = Join-Path $toolPythonRoot 'Scripts/python.exe'
for ($toolAncestor=$toolPythonRoot; $toolAncestor; $toolAncestor=Split-Path -Parent $toolAncestor) {
    if ((Test-Path -LiteralPath $toolAncestor) -and
        ((Get-Item -LiteralPath $toolAncestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Tool output cannot use a linked directory'
    }
    if ($toolAncestor -eq $projectRoot) { break }
}
if (-not (Test-Path -LiteralPath $toolPython)) {
    if (Test-Path -LiteralPath $toolPythonRoot) { throw 'Incomplete tool Python environment; existing files preserved' }
    & py -3.12 -m venv $toolPythonRoot
    if ($LASTEXITCODE -ne 0) { throw 'Cannot create local tool Python environment' }
}
$toolPythonPrefix = & $toolPython -c 'import sys; print(sys.prefix)'
if ($LASTEXITCODE -ne 0 -or [IO.Path]::GetFullPath($toolPythonPrefix) -ne $toolPythonRoot) {
    throw 'Tool Python environment belongs to a different path'
}
$toolPythonLock = Join-Path $projectRoot 'tools/python-tools.lock.txt'
$toolWheelCache = Join-Path $projectRoot 'tools/downloads/python-tools'
New-Item -ItemType Directory -Path $toolWheelCache -Force | Out-Null
& $toolPython -m pip download --disable-pip-version-check --require-hashes --only-binary=:all: --no-deps -r $toolPythonLock -d $toolWheelCache
if ($LASTEXITCODE -ne 0) { throw 'Cannot download the pinned zstd decoder' }
& $toolPython -m pip install --disable-pip-version-check --require-hashes --only-binary=:all: --no-deps --no-index --find-links $toolWheelCache -r $toolPythonLock
if ($LASTEXITCODE -ne 0) { throw 'Cannot install the pinned local zstd decoder' }
$toolArguments = @((Join-Path $PSScriptRoot 'prepare_tools.py'))
if ($CacheFrom) { $toolArguments += @('--cache-from', [IO.Path]::GetFullPath($CacheFrom)) }
& $toolPython @toolArguments
if ($LASTEXITCODE -ne 0) { throw 'Tool preparation failed; existing files were preserved' }
