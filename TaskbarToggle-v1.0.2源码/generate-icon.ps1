# Recreate the embedded icon using simple bitmap geometry; no external packages.
$ErrorActionPreference = 'Stop'
$sizes = @(16, 24, 32, 48, 64)
$images = [System.Collections.Generic.List[byte[]]]::new()
foreach ($size in $sizes) {
    $stream = [System.IO.MemoryStream]::new()
    $writer = [System.IO.BinaryWriter]::new($stream)
    $writer.Write([uint32]40)
    $writer.Write([int32]$size)
    $writer.Write([int32]($size * 2))
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([uint32]0)
    $writer.Write([uint32]($size * $size * 4))
    foreach ($unused in 1..4) { $writer.Write([uint32]0) }
    for ($y = $size - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $size; $x++) {
            $px = ($x + 0.5) * 32.0 / $size
            $py = ($y + 0.5) * 32.0 / $size
            $dx = [Math]::Max(7 - $px, [Math]::Max($px - 25, 0))
            $dy = [Math]::Max(7 - $py, [Math]::Max($py - 25, 0))
            [uint32]$color = 0
            if ($px -ge 2 -and $px -lt 30 -and $py -ge 2 -and $py -lt 30 -and $dx*$dx + $dy*$dy -le 25) {
                $color = [Convert]::ToUInt32('FF000000', 16)
            }
            if (($px -ge 8 -and $px -lt 24 -and $py -ge 9 -and $py -lt 19 -and ($px -lt 9.5 -or $px -ge 22.5 -or $py -lt 10.5 -or $py -ge 17.5)) -or
                ($px -ge 8 -and $px -lt 24 -and $py -ge 22 -and $py -lt 25)) {
                $color = [uint32]::MaxValue
            }
            $writer.Write($color)
        }
    }
    $maskStride = [int]([Math]::Ceiling($size / 32.0) * 4)
    $writer.Write([byte[]]::new($maskStride * $size))
    $images.Add($stream.ToArray())
    $writer.Dispose()
    $stream.Dispose()
}
$output = [System.IO.File]::Create((Join-Path $PSScriptRoot 'app.ico'))
$writer = [System.IO.BinaryWriter]::new($output)
$writer.Write([uint16]0)
$writer.Write([uint16]1)
$writer.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $writer.Write([byte]$sizes[$i])
    $writer.Write([byte]$sizes[$i])
    $writer.Write([byte]0)
    $writer.Write([byte]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([uint32]$images[$i].Length)
    $writer.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach ($image in $images) { $writer.Write([byte[]]$image) }
$writer.Dispose()
$output.Dispose()
