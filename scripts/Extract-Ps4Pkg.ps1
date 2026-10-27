param(
    [Parameter(Mandatory)][string]$InputPkg,
    [Parameter(Mandatory)][string]$OutputDir,
    [string]$PkgTool,
    [string]$Passcode = '00000000000000000000000000000000'
)
# Extract a backup with LibOrbisPkg, including the original PKG metadata.
# Each invocation creates a new output directory and leaves the input untouched.
$ErrorActionPreference = 'Stop'
if (-not $PkgTool) {
    $PkgTool = Join-Path $PSScriptRoot 'pkgtool/PkgTool.exe'
    if (-not (Test-Path -LiteralPath $PkgTool)) {
        $PkgTool = Join-Path (Split-Path -Parent $PSScriptRoot) 'tools/apps/pkgtool-0.2.231/PkgTool.exe'
    }
    if (-not (Test-Path -LiteralPath $PkgTool)) { throw 'PkgTool is missing; extract the complete program package, or prepare the pinned tools in a source checkout.' }
}
$inputPath = (Resolve-Path -LiteralPath $InputPkg).Path
$toolPath = (Resolve-Path -LiteralPath $PkgTool).Path
$outputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
if (Test-Path -LiteralPath $outputPath) { throw 'OutputDir must not already exist.' }
if ($Passcode.Length -ne 32) { throw 'The PKG passcode must contain 32 characters.' }
function Read-Big32([IO.BinaryReader]$Reader) {
    $bytes = $Reader.ReadBytes(4)
    if ($bytes.Length -ne 4) { throw 'Truncated PKG header.' }
    [Array]::Reverse($bytes)
    return [BitConverter]::ToUInt32($bytes, 0)
}
$names = @{
    0x400='license.dat'; 0x401='license.info'; 0x402='nptitle.dat'; 0x403='npbind.dat'
    0x409='psreserved.dat'; 0x1000='param.sfo'; 0x1001='playgo-chunk.dat'
    0x1002='playgo-chunk.sha'; 0x1003='playgo-manifest.xml'; 0x1004='pronunciation.xml'
    0x1005='pronunciation.sig'; 0x1006='pic1.png'; 0x1008='app/playgo-chunk.dat'
    0x100b='shareparam.json'; 0x1200='icon0.png'; 0x1220='pic0.png'; 0x1240='snd0.at9'
    0x1260='changeinfo/changeinfo.xml'; 0x1280='icon0.dds'; 0x12a0='pic0.dds'
    0x12c0='pic1.dds'; 0x1400='trophy/trophy00.trp'
}
$entries = @()
$stream = [IO.File]::OpenRead($inputPath)
$reader = [IO.BinaryReader]::new($stream)
try {
    if ((Read-Big32 $reader) -ne 0x7f434e54) { throw 'Not a PS4 PKG.' }
    $stream.Position = 0x10
    $count = Read-Big32 $reader
    $stream.Position = 0x18
    $table = Read-Big32 $reader
    if ($count -lt 1 -or $count -gt 4096 -or ([long]$table + [long]$count * 32) -gt $stream.Length) {
        throw 'Invalid PKG entry table.'
    }
    for ($index = 0; $index -lt $count; $index++) {
        $stream.Position = [long]$table + [long]$index * 32
        $id = [int](Read-Big32 $reader)
        if ($names.ContainsKey($id)) { $entries += @{ Index=$index; Name=$names[$id] } }
    }
} finally { $reader.Dispose(); $stream.Dispose() }
if (-not ($entries | Where-Object Name -eq 'param.sfo')) { throw 'PKG has no param.sfo.' }
New-Item -ItemType Directory -Path $outputPath | Out-Null
& $toolPath pkg_extract --passcode $Passcode $inputPath $outputPath
if ($LASTEXITCODE -ne 0) { throw 'PkgTool extraction failed; keep the output for diagnosis.' }
$gameRoot = Join-Path $outputPath 'uroot'
if (-not (Test-Path -LiteralPath $gameRoot -PathType Container) -or
    -not (Get-ChildItem -LiteralPath $gameRoot -Force | Select-Object -First 1)) {
    throw 'No game files were extracted. Check the package and passcode.'
}
foreach ($entry in $entries) {
    $destination = Join-Path (Join-Path $gameRoot 'sce_sys') $entry.Name
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
    & $toolPath pkg_extractentry --passcode $Passcode $inputPath ([string]$entry.Index) $destination
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $destination -PathType Leaf)) {
        throw "Metadata extraction failed: $($entry.Name)"
    }
}
Write-Output "Game files: $gameRoot"
Write-Output 'For an update, copy all contents of its uroot over a COPY of the base uroot.'
