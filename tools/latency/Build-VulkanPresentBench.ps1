#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Destination)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Set-Location -LiteralPath $root
. (Join-Path $root 'scripts/Build-Common.ps1')
Initialize-CollectionBuildEnvironment
[Diagnostics.Process]::GetCurrentProcess().PriorityClass='BelowNormal'
$destinationPath=[IO.Path]::GetFullPath($Destination)
$privateRoot=[IO.Path]::GetFullPath((Join-Path $root '.local'))
if(-not $destinationPath.StartsWith($privateRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Use an independent directory inside .local'
}
if(Test-Path -LiteralPath $destinationPath) {throw 'Fresh benchmark build directory required'}
New-Item -ItemType Directory -Path $destinationPath | Out-Null
$compiler=Get-CollectionTool 'clang-cl'
$source=Join-Path $PSScriptRoot 'vulkan_present_bench.cpp'
$snapshots=Join-Path $destinationPath 'sources'
New-Item -ItemType Directory -Path $snapshots | Out-Null
Copy-Item -LiteralPath $source,(Join-Path $PSScriptRoot 'vulkan_present_feedback.h'),(Join-Path $PSScriptRoot 'vulkan_present_wait.h'),$PSCommandPath -Destination $snapshots
$object=Join-Path $destinationPath 'vulkan_present_bench.obj'
$binary=Join-Path $destinationPath 'vulkan-present-bench.exe'
Invoke-Checked $compiler @('/nologo','/std:c++20','/O2','/EHsc','/MD',
    ('/I'+(Join-Path $root 'third_party/rexglue/thirdparty/vulkan-headers/include')),
    ('/Fo'+$object),('/Fe'+$binary),$source,'/link','user32.lib')
Get-FileHash -Algorithm SHA256 -LiteralPath $source,$binary
$identities=@($source,(Join-Path $PSScriptRoot 'vulkan_present_feedback.h'),(Join-Path $PSScriptRoot 'vulkan_present_wait.h'),
    (Join-Path $root 'third_party/rexglue/thirdparty/vulkan-headers/include/vulkan/vulkan_core.h'),$binary) | ForEach-Object {
        @{path=[IO.Path]::GetFullPath($_);sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
@{schema=1;files=$identities;compiler=$compiler;private_only=$true} | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath (Join-Path $destinationPath 'build-result.json') -Encoding utf8
