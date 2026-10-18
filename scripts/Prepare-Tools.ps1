#requires -Version 7.0
[CmdletBinding()]
param([string]$CacheFrom)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$arguments = @('-3.12', (Join-Path $PSScriptRoot 'prepare_tools.py'))
if ($CacheFrom) { $arguments += @('--cache-from', [IO.Path]::GetFullPath($CacheFrom)) }
& py @arguments
if ($LASTEXITCODE -ne 0) { throw 'Tool preparation failed; existing files were preserved' }
