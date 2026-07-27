# Generates src/app.ico — a 7-frame terminal-tile mark in the brand coral.
# Output is a single ICO containing 16, 24, 32, 48, 64, 128, and 256 px
# frames so the shell scales cleanly across the taskbar, ALT-TAB, and
# Explorer thumbnails.
#
# Brand reference: palette_.accent in light mode = RGB(217, 119, 87) (coral).
# We pair the coral with a soft cream background to read as a Settings
# chrome chip rather than an OS warning sign.
#
# Format note: we emit each frame as a 32bpp BITMAPINFOHEADER DIB inside the
# ICO container. The Windows Resource Compiler (rc.exe) rejects PNG-in-ICO
# with RC2176 ("old DIB"), so we use the legacy DIB layout it understands:
#   - BITMAPINFOHEADER (40 bytes) — height is doubled (icon + AND mask plane)
#   - Pixel data is BGRA, top-down rows, padded to 4-byte boundary
#   - AND mask is 1 bpp, padded to 4-byte row stride, all zero (visible)
# Vista+ does not need the AND mask for 32bpp icons, but we still emit it
# so rc.exe does not complain about missing mask data.

param(
    [string]$OutPath = "$PSScriptRoot\..\src\app.ico"
)

Add-Type -AssemblyName System.Drawing

function New-TileIcon([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

    # Brand palette (mirrors NFUI light mode).
    $bgColor     = [System.Drawing.Color]::FromArgb(255, 250, 249, 245)
    $borderColor = [System.Drawing.Color]::FromArgb(255, 219, 215, 204)
    $accent      = [System.Drawing.Color]::FromArgb(255, 217, 119, 87)
    $accentDark  = [System.Drawing.Color]::FromArgb(255, 193, 95, 63)
    $accentLight = [System.Drawing.Color]::FromArgb(255, 232, 162, 142)

    # Cream rounded-square background.
    $radius = [Math]::Max(2, [int]($size * 0.18))
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc(0, 0, $radius, $radius, 180, 90)
    $path.AddArc($size - $radius, 0, $radius, $radius, 270, 90)
    $path.AddArc($size - $radius, $size - $radius, $radius, $radius, 0, 90)
    $path.AddArc(0, $size - $radius, $radius, $radius, 90, 90)
    $path.CloseFigure()
    $bgBrush = New-Object System.Drawing.SolidBrush($bgColor)
    $borderPen = New-Object System.Drawing.Pen($borderColor, [Math]::Max(1, [int]($size * 0.04)))
    $g.FillPath($bgBrush, $path)
    $g.DrawPath($borderPen, $path)
    $bgBrush.Dispose()
    $borderPen.Dispose()
    $path.Dispose()

    # 2x2 terminal tiles — accent-dominant so the active tile (top-left)
    # reads first, the other three in lighter shades for depth.
    $pad = [int]($size * 0.20)
    $tileSize = ($size - 2 * $pad - [int]($size * 0.08)) / 2
    $gap = [int]($size * 0.08)

    $accentBrush      = New-Object System.Drawing.SolidBrush($accent)
    $accentDarkBrush  = New-Object System.Drawing.SolidBrush($accentDark)
    $accentLightBrush = New-Object System.Drawing.SolidBrush($accentLight)

    $g.FillRectangle($accentBrush,      $pad,                          $pad,                          $tileSize, $tileSize)
    $g.FillRectangle($accentLightBrush, ($pad + $tileSize + $gap),     $pad,                          $tileSize, $tileSize)
    $g.FillRectangle($accentLightBrush, $pad,                          ($pad + $tileSize + $gap),     $tileSize, $tileSize)
    $g.FillRectangle($accentDarkBrush,  ($pad + $tileSize + $gap),     ($pad + $tileSize + $gap),     $tileSize, $tileSize)

    $accentBrush.Dispose()
    $accentDarkBrush.Dispose()
    $accentLightBrush.Dispose()

    # ">_" prompt glyph on the primary tile for terminal flavor.
    if ($size -ge 24) {
        $fontSize = [Math]::Max(6, [int]($tileSize * 0.5))
        $font = New-Object System.Drawing.Font("Consolas", $fontSize, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
        $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
        $sf = New-Object System.Drawing.StringFormat
        $sf.Alignment = [System.Drawing.StringAlignment]::Center
        $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
        $r1 = New-Object System.Drawing.RectangleF($pad, $pad, $tileSize, $tileSize)
        $g.DrawString(">", $font, $brush, $r1, $sf)
        $font.Dispose()
        $brush.Dispose()
        $sf.Dispose()
    }

    $g.Dispose()
    return $bmp
}

# Encode a 32bpp bitmap as a BMP-in-ICO DIB payload.
#   - BITMAPINFOHEADER (40 bytes) with biHeight = 2*size (icon + AND mask plane)
#   - XOR mask: BGRA pixels, bottom-up rows, 4-byte aligned stride
#   - AND mask: 1 bpp, all zeros (fully opaque), 4-byte aligned row stride
# rc.exe accepts this layout; PNG-in-ICO triggers RC2176.
function ConvertTo-IcoDib([System.Drawing.Bitmap]$bmp) {
    $w = $bmp.Width
    $h = $bmp.Height
    # 4-byte aligned row stride in BYTES (32bpp = 4 bytes/pixel).
    $xorRowBytes = (((32 * $w + 31) -band (-bnot 31)) -shr 3)
    $xorSize = $xorRowBytes * $h
    $andRowBytes = (((1 * $w + 31) -band (-bnot 31)) -shr 3)
    $andSize = $andRowBytes * $h

    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)

    # BITMAPINFOHEADER — note biHeight is doubled to include the AND mask.
    $bw.Write([uint32]40)              # biSize
    $bw.Write([int32]$w)               # biWidth
    $bw.Write([int32](2 * $h))         # biHeight (image + mask)
    $bw.Write([uint16]1)               # biPlanes
    $bw.Write([uint16]32)              # biBitCount
    $bw.Write([uint32]0)               # biCompression = BI_RGB
    $bw.Write([uint32]$xorSize)        # biSizeImage
    $bw.Write([int32]0)                # biXPelsPerMeter
    $bw.Write([int32]0)                # biYPelsPerMeter
    $bw.Write([uint32]0)               # biClrUsed
    $bw.Write([uint32]0)               # biClrImportant

    # XOR mask (pixel data) — bottom-up, BGRA.
    $bd = $bmp.LockBits([System.Drawing.Rectangle]::new(0, 0, $w, $h),
        [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $rowBytes = $bd.Stride
        $totalBytes = $rowBytes * $h
        $buf = [byte[]]::new($totalBytes)
        [System.Runtime.InteropServices.Marshal]::Copy($bd.Scan0, $buf, 0, $totalBytes)
        for ($y = $h - 1; $y -ge 0; $y--) {
            $srcOffset = $y * $rowBytes
            for ($x = 0; $x -lt $w; $x++) {
                $i = $srcOffset + $x * 4
                $bw.Write([byte]$buf[$i])        # B
                $bw.Write([byte]$buf[$i + 1])    # G
                $bw.Write([byte]$buf[$i + 2])    # R
                $bw.Write([byte]$buf[$i + 3])    # A
            }
            # Pad row to 4-byte boundary if needed (here w is a multiple of 4
            # for our sizes, but keep it safe).
            $pad = $xorRowBytes - ($w * 4)
            for ($i = 0; $i -lt $pad; $i++) { $bw.Write([byte]0) }
        }
    } finally {
        $bmp.UnlockBits($bd)
    }

    # AND mask — 1 bpp, fully opaque (all zeros), 4-byte aligned rows.
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $andRowBytes; $x++) { $bw.Write([byte]0) }
    }

    $bw.Flush()
    $bytes = $ms.ToArray()
    $bw.Dispose()
    $ms.Dispose()
    # PowerShell unwraps [byte[]] casts on function return. Use the comma
    # operator (`return ,$x`) so the inner byte array stays a single-element
    # array-of-byte[] that callers can index into without type coercion.
    return ,$bytes
}

# Build all frame sizes.
$sizes = @(16, 24, 32, 48, 64, 128, 256)
$bitmaps = @()
$payloads = @()
foreach ($s in $sizes) {
    $bmp = New-TileIcon $s
    $bitmaps += ,$bmp
    $payloads += ,(ConvertTo-IcoDib $bmp)
}

# Write ICO container: 6-byte header + N * 16-byte directory entries + payloads.
$out = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($out)
$bw.Write([uint16]0)                  # reserved
$bw.Write([uint16]1)                  # type: 1 = icon
$bw.Write([uint16]$bitmaps.Count)     # count

$offset = 6 + 16 * $bitmaps.Count
for ($i = 0; $i -lt $bitmaps.Count; $i++) {
    $w = $bitmaps[$i].Width
    $h = $bitmaps[$i].Height
    # Width / height: 0 in the directory entry means 256 (single byte field).
    $wByte = if ($w -ge 256) { 0 } else { $w }
    $hByte = if ($h -ge 256) { 0 } else { $h }
    $bw.Write([byte]$wByte)
    $bw.Write([byte]$hByte)
    $bw.Write([byte]0)                # color count (0 for >=8bpp)
    $bw.Write([byte]0)                # reserved
    $bw.Write([uint16]1)              # planes
    $bw.Write([uint16]32)             # bits per pixel
    $bw.Write([uint32]$payloads[$i].Length)
    $bw.Write([uint32]$offset)
    $offset += $payloads[$i].Length
}
foreach ($p in $payloads) { $bw.Write($p) }

$bw.Flush()
[byte[]]$bytes = $out.ToArray()
$out.Dispose()
$bw.Dispose()

[System.IO.File]::WriteAllBytes($OutPath, $bytes)

foreach ($bmp in $bitmaps) { $bmp.Dispose() }

Write-Host "Wrote $OutPath ($($bytes.Length) bytes, $($bitmaps.Count) frames)"