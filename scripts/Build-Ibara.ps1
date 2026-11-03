#requires -Version 7.0
[CmdletBinding()]
param([ValidateRange(1,8)][int]$Jobs=2,[string]$DataDirectory,[switch]$NativeOnly)
. (Join-Path $PSScriptRoot 'Build-Common.ps1')
Initialize-CollectionBuildEnvironment
$cmake=Get-CollectionTool 'cmake'
$taskBuild=Join-Path $CollectionRoot 'out/ps2-tools'
if($NativeOnly) {
    $taskCache=Join-Path $taskBuild 'CMakeCache.txt'
    if(-not (Test-Path -LiteralPath $taskCache) -or
       -not (Select-String -LiteralPath $taskCache -Pattern '^PS2_IBARA_GENERATED:PATH=.+')) {
        throw 'Build Ibara once before using -NativeOnly'
    }
    Invoke-Checked $cmake @('--build',$taskBuild,'--target','ps2_recomp','--parallel',"$Jobs")
    $taskState=Join-Path $CollectionRoot ('.local/build-inputs/ps2-'+[guid]::NewGuid().ToString('N')+'.json')
    Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'ps2_build_receipt.py'),'begin','--state',$taskState)
    $taskTargets=@('--build',$taskBuild,'--target','ps2_ibara_probe')
    if(Select-String -LiteralPath $taskCache -Pattern '^PS2_GAME_GENERATED:PATH=.+'){$taskTargets+='ps2_espgaluda_probe'}
    $taskTargets+=@('--parallel',"$Jobs")
    Invoke-Checked $cmake $taskTargets
    Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'ps2_build_receipt.py'),'finish','--state',$taskState,'--stage')
    return
}
if(-not $DataDirectory){$DataDirectory=Join-Path $CollectionRoot 'game-data/ibara'}
$taskIso=& py -3.12 (Join-Path $PSScriptRoot 'ps2_ibara.py') --find-iso $DataDirectory
if($LASTEXITCODE){throw 'Could not find one supported Ibara ISO'}
$taskIso=$taskIso.Trim()
if(-not (Test-Path -LiteralPath (Join-Path $taskBuild 'CMakeCache.txt'))) {
    & (Join-Path $PSScriptRoot 'Build-Ps2.ps1') -ToolsOnly -Jobs $Jobs
} else {
    Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'ps2_sources.py'),'restore')
    Invoke-Checked $cmake @('--build',$taskBuild,'--target','ps2_recomp','--parallel',"$Jobs")
}
$taskPrepared=& py -3.12 (Join-Path $PSScriptRoot 'ps2_ibara.py') --default-output
if($LASTEXITCODE){throw 'Could not identify Ibara build inputs'}
$taskPrepared=$taskPrepared.Trim()
if(-not (Test-Path -LiteralPath (Join-Path $taskPrepared 'plan.json'))) {
    Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'ps2_ibara.py'),'--iso',$taskIso,'--output',$taskPrepared,'--prepare-only')
}
$taskInputs=& py -3.12 (Join-Path $PSScriptRoot 'ps2_ibara.py') --output $taskPrepared
if($LASTEXITCODE){throw 'Ibara code generation failed'}
$taskInputs=($taskInputs -join "`n") | ConvertFrom-Json
& (Join-Path $PSScriptRoot 'Build-Ps2.ps1') -Jobs $Jobs -IbaraGenerated $taskInputs.ibara
Write-Host "Built Ibara: $taskBuild/bin/ibara.exe"
