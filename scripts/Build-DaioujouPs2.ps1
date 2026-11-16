#requires -Version 7.0
[CmdletBinding()]
param([ValidateRange(1,8)][int]$Jobs=2,[string]$DataDirectory,[switch]$NativeOnly)
. (Join-Path $PSScriptRoot 'Build-Common.ps1')
Initialize-CollectionBuildEnvironment
$cmake=Get-CollectionTool 'cmake'
$taskBuild=Join-Path $CollectionRoot 'out/ps2-tools'
$taskCache=Join-Path $taskBuild 'CMakeCache.txt'
if($NativeOnly) {
    if(-not (Test-Path -LiteralPath $taskCache)){throw 'Build DaiOuJou once before using -NativeOnly'}
    $taskPrograms=@{}
    foreach($line in Get-Content -LiteralPath $taskCache){
        if($line -match '^PS2_DAIOUJOU(?:_(DEATH|GALLERY))?_GENERATED:PATH=(.+)$'){
            $taskName=if($Matches[1]){$Matches[1].ToLowerInvariant()}else{'boot'}
            $taskPrograms[$taskName]=$Matches[2]
        }
    }
    if($taskPrograms.Count -ne 3){throw 'Build all three DaiOuJou disc programs before using -NativeOnly'}
} else {
    if(-not $DataDirectory){$DataDirectory=Join-Path $CollectionRoot 'game-data/daioujou-ps2'}
    $taskScript=Join-Path $PSScriptRoot 'ps2_daioujou.py'
    $taskIso=& py -3.12 $taskScript --find-iso $DataDirectory
    if($LASTEXITCODE){throw 'Could not find one supported DaiOuJou PS2 ISO'}
    if(-not (Test-Path -LiteralPath $taskCache)){
        & (Join-Path $PSScriptRoot 'Build-Ps2.ps1') -ToolsOnly -Jobs $Jobs
    } else {
        Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'ps2_sources.py'),'restore')
        Invoke-Checked $cmake @('--build',$taskBuild,'--target','ps2_recomp','--parallel',"$Jobs")
    }
    $taskPrograms=& py -3.12 $taskScript --iso $taskIso.Trim()
    if($LASTEXITCODE){throw 'DaiOuJou PS2 code generation failed'}
    $taskPrograms=$taskPrograms | ConvertFrom-Json -AsHashtable
}
& (Join-Path $PSScriptRoot 'Build-Ps2.ps1') -Jobs $Jobs -DaioujouPrograms $taskPrograms
Write-Host "Built DaiOuJou PS2: $taskBuild/bin/daioujou-ps2.exe"
