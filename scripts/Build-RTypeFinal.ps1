#requires -Version 7.0
[CmdletBinding()]
param([ValidateRange(1,8)][int]$Jobs=2,[string]$DataDirectory,[switch]$NativeOnly)
. (Join-Path $PSScriptRoot 'Build-Common.ps1')
Initialize-CollectionBuildEnvironment
$cmake=Get-CollectionTool 'cmake'
$taskBuild=Join-Path $CollectionRoot 'out/ps2-tools'
$taskCache=Join-Path $taskBuild 'CMakeCache.txt'
if($NativeOnly) {
    if(-not (Test-Path -LiteralPath $taskCache)){throw 'Build R-Type Final once before using -NativeOnly'}
    $taskPrograms=@{}
    foreach($line in Get-Content -LiteralPath $taskCache){
        if($line -match '^PS2_RTYPE_FINAL_GENERATED:PATH=(.+)$'){$taskPrograms.boot=$Matches[1]}
        if($line -match '^PS2_RTYPE_FINAL_BANKS:FILEPATH=(.+)$'){$taskPrograms.banks=$Matches[1]}
        if($line -match '^PS2_RTYPE_FINAL_VU:PATH=(.+)$'){$taskPrograms.vu=$Matches[1]}
    }
    if($taskPrograms.Count -ne 3){throw 'Build all R-Type Final disc programs before using -NativeOnly'}
} else {
    if(-not $DataDirectory){$DataDirectory=Join-Path $CollectionRoot 'game-data/rtype-final'}
    $taskScript=Join-Path $PSScriptRoot 'ps2_rtype.py'
    $taskIso=& py -3.12 $taskScript --find-iso $DataDirectory
    if($LASTEXITCODE){throw 'Could not find one supported R-Type Final ISO'}
    if(-not (Test-Path -LiteralPath $taskCache)){
        & (Join-Path $PSScriptRoot 'Build-Ps2.ps1') -ToolsOnly -Jobs $Jobs
    } else {
        Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'ps2_sources.py'),'restore')
        Invoke-Checked $cmake @('--build',$taskBuild,'--target','ps2_recomp','ps2_vu_aot','--parallel',"$Jobs")
    }
    $taskPrograms=& py -3.12 $taskScript --iso $taskIso.Trim()
    if($LASTEXITCODE){throw 'R-Type Final code generation failed'}
    $taskPrograms=$taskPrograms | ConvertFrom-Json -AsHashtable
}
& (Join-Path $PSScriptRoot 'Build-Ps2.ps1') -Jobs $Jobs -RTypePrograms $taskPrograms
