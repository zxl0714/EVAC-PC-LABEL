#requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateRange(1,8)][int]$Jobs=2,
    [ValidateSet('espgaluda2')][string[]]$Games=@(),
    [switch]$NativeOnly,
    [string]$DataDirectory
)
. (Join-Path $PSScriptRoot 'Build-Common.ps1')
$taskProcess = Get-Process -Id $PID
$previousPriority = $taskProcess.PriorityClass
try {
    $taskProcess.PriorityClass = 'BelowNormal'
    Initialize-CollectionBuildEnvironment
    $cmake = Get-CollectionTool 'cmake'
    if ($NativeOnly -and -not $Games.Count) { throw '-NativeOnly requires -Games' }
    $selected=@($Games | Sort-Object -Unique)
    $buildName=if (-not $selected.Count) { 'common' } elseif ($NativeOnly) { 'espgaluda2-native' } else { 'espgaluda2' }
    $build = Join-Path $CollectionRoot ('out/' + $buildName)
    if ($selected.Count) {
        Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'build_receipt.py'),'verify-sdk')
        if (-not $NativeOnly) {
            if (-not $DataDirectory) { $DataDirectory=Join-Path $CollectionRoot 'game-data/espgaluda2' }
            & (Join-Path $PSScriptRoot 'Generate.ps1') -Game espgaluda2 -DataDirectory $DataDirectory
            $receiptState=Join-Path $CollectionRoot ('.local/build-inputs/host-' + [guid]::NewGuid().ToString('N') + '.json')
            Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'build_receipt.py'),'begin','host','--state',$receiptState)
        }
    }
    $configure=@('-S',$CollectionRoot,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
        ('-DCMAKE_CXX_COMPILER=' + (Get-CollectionTool 'clang++')),
        ('-DCMAKE_MAKE_PROGRAM=' + (Get-CollectionTool 'ninja')),
        ('-DCAVE_GAMES=' + ($selected -join ';')),
        '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG -g -gcodeview',
        '-DCMAKE_EXE_LINKER_FLAGS=-Xlinker /DEBUG:FULL -Xlinker /OPT:REF -Xlinker /OPT:ICF',
        '-DCMAKE_SHARED_LINKER_FLAGS=-Xlinker /DEBUG:FULL -Xlinker /OPT:REF -Xlinker /OPT:ICF')
    if ($selected.Count) {
        $configure+=@(('-DCMAKE_PREFIX_PATH=' + (Join-Path $CollectionRoot '.deps/sdk-install')),
            ('-DCMAKE_C_COMPILER=' + (Get-CollectionTool 'clang')),
            ('-DCAVE_BUILD_GAME=' + $(if ($NativeOnly) { 'OFF' } else { 'ON' })))
    }
    Invoke-Checked $cmake $configure
    Invoke-Checked $cmake @('--build',$build,'--parallel',"$Jobs")
    if ($selected.Count -and -not $NativeOnly) {
        # GPU plugins are loaded at runtime: refresh them even on a no-op host build.
        foreach ($component in @('rexruntime','rexgpu-xenos')) {
            foreach ($extension in @('dll','pdb')) {
                Copy-Item -LiteralPath (Join-Path $CollectionRoot ".deps/sdk-install/bin/$component.$extension") -Destination (Join-Path $build 'bin')
            }
        }
        Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'build_receipt.py'),'finish','host','--state',$receiptState)
        Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'stage_game.py'))
    }
    Write-Host "Built under $build/bin. This command does not launch a game."
} finally { $taskProcess.PriorityClass = $previousPriority }
