#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Binary,
      [Parameter(Mandatory)][string]$Destination,
      [ValidateRange(3,60)][int]$Seconds=12)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Set-Location -LiteralPath $root
$binaryPath=(Resolve-Path -LiteralPath $Binary).Path
$outputPath=[IO.Path]::GetFullPath($Destination)
$privateRoot=[IO.Path]::GetFullPath((Join-Path $root '.local'))
if(-not $outputPath.StartsWith($privateRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Use an independent directory inside .local'
}
if(Test-Path -LiteralPath $outputPath) {throw 'Fresh benchmark run directory required'}
New-Item -ItemType Directory -Path $outputPath | Out-Null
$identities=@($binaryPath,(Join-Path $PSScriptRoot 'vulkan_present_bench.cpp'),
    (Join-Path $PSScriptRoot 'vulkan_present_feedback.h'),
    (Join-Path $root 'scripts/vulkan_present_feedback.py')) | ForEach-Object {
        $item=Get-Item -LiteralPath $_
        @{path=$item.FullName;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
$results=@()
@{schema=1;seconds_per_run=$Seconds;identities=$identities} |
    ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $outputPath 'inputs.json') -Encoding utf8
foreach($fullscreen in @($false,$true)) {
    foreach($mode in @('fifo','mailbox','immediate')) {
        $label=$mode+'-'+$(if($fullscreen){'fullscreen'}else{'window'})
        $capture=Join-Path $outputPath ($label+'.jsonl')
        $report=Join-Path $outputPath ($label+'-summary.json')
        $arguments=@('--output',$capture,'--mode',$mode,'--seconds',[string]$Seconds)
        if($fullscreen){$arguments+='--fullscreen'}
        & $binaryPath @arguments > (Join-Path $outputPath ($label+'.log'))
        if($LASTEXITCODE -ne 0){throw "Benchmark failed: $label"}
        $errorLog=Join-Path $outputPath ($label+'-analysis-errors.log')
        & py -3.12 (Join-Path $root 'scripts/vulkan_present_feedback.py') $capture --output $report > (Join-Path $outputPath ($label+'-analysis.log')) 2> $errorLog
        if($LASTEXITCODE -ne 0) {
            $results+=@{label=$label;capture=$capture;analysis_valid=$false;error_log=$errorLog;
                capture_sha256=(Get-FileHash -LiteralPath $capture -Algorithm SHA256).Hash.ToLowerInvariant()}
            Write-Host "$label : timing feedback rejected; raw capture retained"
            continue
        }
        $data=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
        $metric=$data.metrics.present_to_first_pixel_out_ms
        $results+=@{label=$label;pid=$data.pid;capture=$capture;summary=$report;analysis_valid=$true;
            capture_sha256=$data.source_sha256;p50_ms=$metric.p50_ms;p95_ms=$metric.p95_ms;samples=$metric.samples}
        Write-Host ("{0}: output p50={1:F3}ms p95={2:F3}ms n={3}" -f $label,$metric.p50_ms,$metric.p95_ms,$metric.samples)
    }
}
@{schema=1;seconds_per_run=$Seconds;identities=$identities;results=$results;
  scope='Independent WSI benchmark; not game performance or input latency'} |
    ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $outputPath 'matrix.json') -Encoding utf8
