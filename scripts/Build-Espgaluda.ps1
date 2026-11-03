#requires -Version 7.0
[CmdletBinding()]
param([ValidateRange(1,8)][int]$Jobs=2,[string]$DataDirectory,[switch]$NativeOnly)
. (Join-Path $PSScriptRoot 'Build-Common.ps1')
Initialize-CollectionBuildEnvironment
$cmake=Get-CollectionTool 'cmake'
$taskBuild=Join-Path $CollectionRoot 'out/ps2-tools'
function Invoke-CertifiedEspgaludaBuild {
    # Let Ninja refresh a changed CMake policy before capturing the build inputs.
    Invoke-Checked $cmake @('--build',$taskBuild,'--target','ps2_recomp','--parallel',"$Jobs")
    $taskReceiptState=Join-Path $CollectionRoot ('.local/build-inputs/ps2-'+[guid]::NewGuid().ToString('N')+'.json')
    Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'ps2_build_receipt.py'),'begin','--state',$taskReceiptState)
    $taskTargets=@('--build',$taskBuild,'--target','ps2_espgaluda_probe')
    if(Select-String -LiteralPath (Join-Path $taskBuild 'CMakeCache.txt') -Pattern '^PS2_IBARA_GENERATED:PATH=.+'){$taskTargets+='ps2_ibara_probe'}
    $taskTargets+=@('--parallel',"$Jobs")
    Invoke-Checked $cmake $taskTargets
    Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'ps2_build_receipt.py'),'finish','--state',$taskReceiptState,'--stage')
}
if($NativeOnly) {
    if(-not (Test-Path -LiteralPath (Join-Path $taskBuild 'CMakeCache.txt'))) {
        throw 'Build ESPGaluda once before using -NativeOnly'
    }
    Invoke-CertifiedEspgaludaBuild
    return
}
if(-not $DataDirectory){$DataDirectory=Join-Path $CollectionRoot 'game-data/espgaluda'}
$iso=& py -3.12 (Join-Path $PSScriptRoot 'ps2_game.py') --find-iso $DataDirectory
if($LASTEXITCODE){throw 'Could not find one supported ESPGaluda ISO'}
$iso=$iso.Trim()
if(-not (Test-Path -LiteralPath (Join-Path $taskBuild 'CMakeCache.txt'))) {
    & (Join-Path $PSScriptRoot 'Build-Ps2.ps1') -ToolsOnly -Jobs $Jobs
} else {
    Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'ps2_sources.py'),'restore')
    Invoke-Checked $cmake @('--build',$taskBuild,'--target','ps2_recomp','--parallel',"$Jobs")
}
$prepared=& py -3.12 (Join-Path $PSScriptRoot 'ps2_game.py') --default-output
if($LASTEXITCODE){throw 'Could not identify ESPGaluda build inputs'}
$prepared=$prepared.Trim()
if(-not (Test-Path -LiteralPath (Join-Path $prepared 'plan.json'))) {
    Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'ps2_game.py'),'--iso',$iso,'--output',$prepared,'--prepare-only')
}
$inputs=& py -3.12 (Join-Path $PSScriptRoot 'ps2_game.py') --output $prepared
if($LASTEXITCODE){throw 'ESPGaluda code generation failed'}
$inputs=($inputs -join "`n") | ConvertFrom-Json
$taskExpected=@{PS2_GAME_GENERATED=$inputs.boot;PS2_ARCADE_GENERATED=$inputs.arcade;
    PS2_ARRANGE_GENERATED=$inputs.arrange;PS2_STAGE_MODULES_MANIFEST=$inputs.stages;
    PS2_MODE_EXTRAS_MANIFEST=$inputs.extras;PS2_GAME_SYMBOLS='ON';PS2_RUNTIME_THINLTO='ON';
    PS2_ESPGALUDA_INPUT_ORDER='late-v1'}
$taskCache=@{}
foreach($line in Get-Content -LiteralPath (Join-Path $taskBuild 'CMakeCache.txt')) {
    if($line -match '^([^/#][^:]*):[^=]+=(.*)$'){$taskCache[$Matches[1]]=$Matches[2]}
}
$taskSame=$true
foreach($key in $taskExpected.Keys){
    if(-not $taskCache.ContainsKey($key) -or
       $taskCache[$key].Replace('\','/') -ne $taskExpected[$key].Replace('\','/')){$taskSame=$false;break}
}
if($taskSame){
    # Ninja tracks CMake sources and copied headers itself. Reuse its current
    # graph when the reviewed generated input set has not changed.
    Invoke-CertifiedEspgaludaBuild
} else {
    & (Join-Path $PSScriptRoot 'Build-Ps2.ps1') -Jobs $Jobs -ProbeGenerated $inputs.boot `
        -ArcadeGenerated $inputs.arcade -ArrangeGenerated $inputs.arrange `
        -StageModulesManifest $inputs.stages -ModeExtrasManifest $inputs.extras
}
Write-Host "Built ESPGaluda: $taskBuild/bin/espgaluda.exe"
