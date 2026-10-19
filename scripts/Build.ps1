#requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateRange(1,8)][int]$Jobs=2,
    [ValidateSet('espgaluda2','futari','ketsui')][string[]]$Games=@(),
    [switch]$NativeOnly,
    [switch]$LauncherOnly,
    [string]$DataDirectory,
    [hashtable]$DataDirectories=@{}
)
. (Join-Path $PSScriptRoot 'Build-Common.ps1')
$taskProcess = Get-Process -Id $PID
$previousPriority = $taskProcess.PriorityClass
try {
    $taskProcess.PriorityClass = 'BelowNormal'
    Initialize-CollectionBuildEnvironment
    $cmake = Get-CollectionTool 'cmake'
    if ($NativeOnly -and -not $Games.Count) { throw '-NativeOnly requires -Games' }
    if ($LauncherOnly -and $Games.Count) { throw '-LauncherOnly cannot be combined with -Games' }
    if ($DataDirectory -and $Games.Count -ne 1) { throw '-DataDirectory requires exactly one game; use -DataDirectories for multiple games' }
    if ('ketsui' -in $Games) {
        & (Join-Path $PSScriptRoot 'Build-Ps4.ps1') -Jobs $Jobs
        # PS4 preparation happens natively on first launch. Building the runtime
        # needs no ROM, and does not modify config.ini or any supplied data path.
        $Games = @($Games | Where-Object { $_ -ne 'ketsui' })
        if (-not $Games.Count) { $DataDirectory = $null }
    }
    $selected=@($Games | Sort-Object -Unique)
    if ($DataDirectory -and $selected.Count -ne 1) { throw '-DataDirectory requires exactly one game; use -DataDirectories for multiple games' }
    $nativeName=if ($selected.Count -gt 1) { 'xbox360' } elseif ($selected.Count) { $selected[0] } else { 'common' }
    $buildName=if (-not $selected.Count) { 'common' } elseif ($NativeOnly) { "$nativeName-native" } else { $nativeName }
    $build = Join-Path $CollectionRoot ('out/' + $buildName)
    if ($selected.Count) {
        Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'build_receipt.py'),'verify-sdk')
        if (-not $NativeOnly) {
            foreach ($game in $selected) {
                $inputDirectory=if ($DataDirectory) { $DataDirectory } elseif ($DataDirectories.ContainsKey($game)) {
                    $DataDirectories[$game]
                } else { Join-Path $CollectionRoot "game-data/$game" }
                & (Join-Path $PSScriptRoot 'Generate.ps1') -Game $game -DataDirectory $inputDirectory
            }
            $receiptState=Join-Path $CollectionRoot ('.local/build-inputs/host-' + [guid]::NewGuid().ToString('N') + '.json')
            Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'build_receipt.py'),'begin','host','--state',$receiptState,'--games',($selected -join ','))
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
    $targets=@('--build',$build,'--parallel',"$Jobs")
    if ($LauncherOnly) { $targets+=@('--target','cave_launcher') }
    Invoke-Checked $cmake $targets
    foreach ($extension in @('exe','pdb')) {
        $launcherSource=Join-Path $build "bin/CAVE-PC-Collection.$extension"
        $launcherTarget=Join-Path $CollectionRoot "CAVE-PC-Collection.$extension"
        if ((Test-Path -LiteralPath $launcherTarget) -and
            (Get-FileHash -LiteralPath $launcherSource).Hash -eq (Get-FileHash -LiteralPath $launcherTarget).Hash) { continue }
        Copy-Item -LiteralPath $launcherSource -Destination $launcherTarget
    }
    if ($selected.Count -and -not $NativeOnly) {
        # GPU plugins are loaded at runtime: refresh them even on a no-op host build.
        foreach ($component in @('rexruntime','rexgpu-xenos')) {
            foreach ($extension in @('dll','pdb')) {
                Copy-Item -LiteralPath (Join-Path $CollectionRoot ".deps/sdk-install/bin/$component.$extension") -Destination (Join-Path $build 'bin')
            }
        }
        Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'build_receipt.py'),'finish','host','--state',$receiptState,'--games',($selected -join ','))
        Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'stage_game.py'),'--games',($selected -join ','))
    }
    Write-Host "Built under $build/bin. This command does not launch a game."
} finally { $taskProcess.PriorityClass = $previousPriority }
