#requires -Version 7.0
[CmdletBinding()]
param([ValidateRange(1,8)][int]$Jobs=2)
. (Join-Path $PSScriptRoot 'Build-Common.ps1')
$self = Get-Process -Id $PID
$previousPriority = $self.PriorityClass
try {
    $self.PriorityClass = 'BelowNormal'
    Initialize-CollectionBuildEnvironment
    $status = @(& git -C $CollectionRoot submodule status --recursive)
    if ($LASTEXITCODE -ne 0 -or @($status | Where-Object { $_ -match '^[-+U]' }).Count) {
        throw 'Dependencies are absent or not pinned; run scripts/sdk_sources.py restore'
    }
    $receiptState = Join-Path $CollectionRoot ('.local/build-inputs/sdk-' + [guid]::NewGuid().ToString('N') + '.json')
    Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'build_receipt.py'),'begin','sdk','--state',$receiptState)
    $source = Join-Path $CollectionRoot 'third_party/rexglue'
    $build = Join-Path $CollectionRoot 'out/sdk-build'
    $install = Join-Path $CollectionRoot '.deps/sdk-install'
    $cmake = Get-CollectionTool 'cmake'
    $configure = @('-S',$source,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
        ('-DCMAKE_C_COMPILER=' + (Get-CollectionTool 'clang')), ('-DCMAKE_CXX_COMPILER=' + (Get-CollectionTool 'clang++')),
        ('-DCMAKE_MAKE_PROGRAM=' + (Get-CollectionTool 'ninja')), ('-DCMAKE_INSTALL_PREFIX=' + $install),
        '-DREXGLUE_VERSION_OVERRIDE=0.9.0.0', '-DCMAKE_INSTALL_MESSAGE=LAZY',
        '-DCMAKE_C_FLAGS_RELEASE=-O3 -DNDEBUG -g -gcodeview',
        '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG -g -gcodeview -mssse3 -msse4.1',
        '-DCMAKE_SHARED_LINKER_FLAGS=-Xlinker /DEBUG:FULL -Xlinker /OPT:REF -Xlinker /OPT:ICF',
        '-DCMAKE_EXE_LINKER_FLAGS=-Xlinker /DEBUG:FULL -Xlinker /OPT:REF -Xlinker /OPT:ICF',
        '-DREXGLUE_USE_D3D12=ON', '-DREXGLUE_USE_VULKAN=OFF',
        '-DREXGLUE_ENABLE_FIDELITYFX=OFF', '-DREXGLUE_ENABLE_TRACY=OFF', '-DREXGLUE_BUILD_TESTS=OFF')
    Invoke-Checked $cmake $configure
    Invoke-Checked $cmake @('--build',$build,'--parallel',"$Jobs")
    Invoke-Checked $cmake @('--install',$build)
    foreach ($symbols in Get-ChildItem -LiteralPath (Join-Path $build 'bin') -Filter '*.pdb' -File) {
        Copy-Item -LiteralPath $symbols.FullName -Destination (Join-Path $install ('bin/' + $symbols.Name))
    }
    Invoke-Checked 'py' @('-3.12',(Join-Path $PSScriptRoot 'build_receipt.py'),'finish','sdk','--state',$receiptState)
    Write-Host "SDK built and installed under $install"
} finally { $self.PriorityClass = $previousPriority }
