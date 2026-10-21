#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$InputPng,
    [Parameter(Mandatory)][string]$OutputIco
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$iconSource = [System.Drawing.Image]::FromFile((Resolve-Path -LiteralPath $InputPng))
$iconSizes = @(16,20,24,32,40,48,64,128,256)
$iconFrames = [System.Collections.Generic.List[byte[]]]::new()
try {
    if ($iconSource.Width -ne $iconSource.Height) { throw 'Icon source must be square' }
    foreach ($iconSize in $iconSizes) {
        $iconBitmap = [System.Drawing.Bitmap]::new($iconSize,$iconSize,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $iconGraphics = [System.Drawing.Graphics]::FromImage($iconBitmap)
        $iconStream = [System.IO.MemoryStream]::new()
        try {
            $iconGraphics.Clear([System.Drawing.Color]::Transparent)
            $iconGraphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $iconGraphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $iconGraphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $iconGraphics.DrawImage($iconSource,[System.Drawing.Rectangle]::new(0,0,$iconSize,$iconSize))
            $iconBitmap.Save($iconStream,[System.Drawing.Imaging.ImageFormat]::Png)
            $iconFrames.Add($iconStream.ToArray())
        } finally { $iconStream.Dispose(); $iconGraphics.Dispose(); $iconBitmap.Dispose() }
    }
    $iconOutput = [System.IO.File]::Open([System.IO.Path]::GetFullPath($OutputIco),[System.IO.FileMode]::CreateNew)
    $iconWriter = [System.IO.BinaryWriter]::new($iconOutput)
    try {
        $iconWriter.Write([uint16]0); $iconWriter.Write([uint16]1); $iconWriter.Write([uint16]$iconSizes.Count)
        $iconOffset = 6 + 16 * $iconSizes.Count
        for ($iconIndex = 0; $iconIndex -lt $iconSizes.Count; ++$iconIndex) {
            $iconDimension = if ($iconSizes[$iconIndex] -eq 256) { 0 } else { $iconSizes[$iconIndex] }
            $iconWriter.Write([byte]$iconDimension); $iconWriter.Write([byte]$iconDimension)
            $iconWriter.Write([byte]0); $iconWriter.Write([byte]0)
            $iconWriter.Write([uint16]1); $iconWriter.Write([uint16]32)
            $iconWriter.Write([uint32]$iconFrames[$iconIndex].Length); $iconWriter.Write([uint32]$iconOffset)
            $iconOffset += $iconFrames[$iconIndex].Length
        }
        foreach ($iconFrame in $iconFrames) { $iconWriter.Write($iconFrame) }
    } finally { $iconWriter.Dispose() }
} finally { $iconSource.Dispose() }
Get-FileHash -LiteralPath $OutputIco -Algorithm SHA256
