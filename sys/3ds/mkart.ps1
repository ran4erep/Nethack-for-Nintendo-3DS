# NetHack 5.0  3ds/mkart.ps1
#
# Converts optional artwork in sys/3ds/art/ into sys/3ds/3dsart_data.c.
#
#   bg.png          320x240 opaque  -> RGB565 background plate
#   ico_play.png    RGBA    -> icon on the PLAY entry
#   ico_gear.png    RGBA    -> ... OPTIONS
#   ico_power.png   RGBA    -> ... EXIT
#   ico_keyboard.png RGBA   -> optional replacement for the ON/OFF glyph
#
# The icons may be any size as long as all of them agree on it; the renderer
# scales each whole square canvas into place, transparency included.  Drawing
# them large (48x48 and up) and letting it shrink them looks best, so the fine
# detail survives the reduction.
#
# Missing files simply produce a NULL entry; the renderer falls back to its
# built-in procedural artwork.  Run from the repository root:
#
#   powershell -ExecutionPolicy Bypass -File sys/3ds/mkart.ps1

Add-Type -AssemblyName System.Drawing

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$artDir = Join-Path $PSScriptRoot "art"
$outFile = Join-Path $PSScriptRoot "3dsart_data.c"

$notes = @()

function Read-Png($path) {
    if (-not (Test-Path $path)) { return $null }
    $img = [System.Drawing.Bitmap]::new($path)
    # force 32bppArgb so alpha survives
    $bmp = [System.Drawing.Bitmap]::new($img.Width, $img.Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $gr = [System.Drawing.Graphics]::FromImage($bmp)
    $gr.DrawImage($img, 0, 0, $img.Width, $img.Height)
    $gr.Dispose()
    $img.Dispose()
    return $bmp
}

function Emit-Lines($name, [string[]]$values, [string]$type) {
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.AppendLine("static const $type $name[] = {")
    for ($i = 0; $i -lt $values.Count; $i++) {
        if (($i % 12) -eq 0) { [void]$sb.Append("    ") }
        [void]$sb.Append($values[$i])
        if ($i -lt $values.Count - 1) { [void]$sb.Append(", ") }
        if (($i % 12) -eq 11) { [void]$sb.AppendLine() }
    }
    [void]$sb.AppendLine()
    [void]$sb.AppendLine("};")
    return $sb.ToString()
}

# ---------------------------------------------------------------- background
$bgPath = Join-Path $artDir "bg.png"
$bgW = 0; $bgH = 0; $bgValues = $null
$bg = Read-Png $bgPath
if ($bg) {
    $bgW = $bg.Width; $bgH = $bg.Height
    if ($bgW -ne 320 -or $bgH -ne 240) {
        $notes += "note: bg.png is ${bgW}x${bgH}, the renderer will scale it to 320x240"
    }
    $vals = [System.Collections.Generic.List[string]]::new()
    for ($y = 0; $y -lt $bgH; $y++) {
        for ($x = 0; $x -lt $bgW; $x++) {
            $c = $bg.GetPixel($x, $y)
            # NB: PowerShell's shift operators keep the *type* of the left
            # operand, so shifting a [byte] silently wraps to zero.  Cast to
            # Int32 before shifting or the whole plate comes out black.
            $v = (([int]$c.R -shr 3) -shl 11) -bor (([int]$c.G -shr 2) -shl 5) `
                 -bor ([int]$c.B -shr 3)
            $vals.Add(("0x{0:x4}" -f $v))
        }
    }
    $bgValues = $vals.ToArray()
    $bg.Dispose()
    $notes += "bg.png ${bgW}x${bgH} loaded"
} else {
    $notes += "bg.png not found - procedural background"
}

# ---------------------------------------------------------------- icons
$iconNames = @(
    @{ file = "ico_play.png";     sym = "_3ds_art_ico_play";     name = "art_ico_play" },
    @{ file = "ico_gear.png";     sym = "_3ds_art_ico_gear";     name = "art_ico_gear" },
    @{ file = "ico_power.png";    sym = "_3ds_art_ico_power";    name = "art_ico_power" },
    @{ file = "ico_keyboard.png"; sym = "_3ds_art_ico_keyboard"; name = "art_ico_keyboard" }
)

$icoW = 0; $icoH = 0
foreach ($e in $iconNames) {
    $p = Join-Path $artDir $e.file
    $im = Read-Png $p
    if (-not $im) { $notes += "$($e.file) not found"; continue }
    if ($icoW -eq 0) {
        $icoW = $im.Width; $icoH = $im.Height
        if ($icoW -ne $icoH) {
            $notes += "note: icons are ${icoW}x${icoH}; a square canvas is simplest"
        }
    }
    elseif ($im.Width -ne $icoW -or $im.Height -ne $icoH) {
        $notes += "error: $($e.file) is $($im.Width)x$($im.Height), expected ${icoW}x${icoH} - skipped"
        $im.Dispose(); continue
    }
    $vals = [System.Collections.Generic.List[string]]::new()
    $opaque = 0
    for ($y = 0; $y -lt $im.Height; $y++) {
        for ($x = 0; $x -lt $im.Width; $x++) {
            $c = $im.GetPixel($x, $y)
            if ($c.A -gt 0) { $opaque++ }
            # same byte-shift trap as above: Int64 keeps the whole ARGB word,
            # otherwise every pixel ends up with alpha 0 and nothing is drawn
            $v = ([long]$c.A -shl 24) -bor ([long]$c.R -shl 16) `
                 -bor ([long]$c.G -shl 8) -bor [long]$c.B
            $vals.Add(("0x{0:x8}" -f $v))
        }
    }
    if ($opaque -eq $im.Width * $im.Height) {
        $notes += "warning: $($e.file) has no transparency (fully opaque)"
    }
    $im.Dispose()
    $e.values = $vals.ToArray()
    $notes += "$($e.file) ${icoW}x${icoH} loaded ($opaque/$($icoW*$icoH) opaque px)"
}

# ---------------------------------------------------------------- write
$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine("/* NetHack 5.0`t3dsart_data.c */")
[void]$sb.AppendLine("/* GENERATED by sys/3ds/mkart.ps1 - do not edit by hand. */")
[void]$sb.AppendLine("/* NetHack may be freely redistributed.  See license for details. */")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("#include ""hack.h""")
[void]$sb.AppendLine("#include <3ds.h>")
[void]$sb.AppendLine("")

if ($bgValues) {
    [void]$sb.AppendLine("/* ---- background plate ---- */")
    [void]$sb.Append((Emit-Lines "art_bg_px" $bgValues "u16"))
    [void]$sb.AppendLine("")
} else {
    [void]$sb.AppendLine("/* ---- background plate (none supplied) ---- */")
}
[void]$sb.AppendLine("const int _3ds_art_bg_w = $bgW;")
[void]$sb.AppendLine("const int _3ds_art_bg_h = $bgH;")
if ($bgValues) { [void]$sb.AppendLine("const u16 *_3ds_art_bg_px = art_bg_px;") }
else { [void]$sb.AppendLine("const u16 *_3ds_art_bg_px = NULL;") }
[void]$sb.AppendLine("")

foreach ($e in $iconNames) {
    if ($e.ContainsKey("values")) {
        [void]$sb.AppendLine("/* ---- $($e.file) ---- */")
        [void]$sb.Append((Emit-Lines $e.name $e.values "u32"))
        [void]$sb.AppendLine("")
    }
}
[void]$sb.AppendLine("const int _3ds_art_ico_w = $icoW;")
[void]$sb.AppendLine("const int _3ds_art_ico_h = $icoH;")
foreach ($e in $iconNames) {
    if ($e.ContainsKey("values")) { [void]$sb.AppendLine("const u32 *$($e.sym) = $($e.name);") }
    else { [void]$sb.AppendLine("const u32 *$($e.sym) = NULL;") }
}
[void]$sb.AppendLine("")
[void]$sb.AppendLine("/* 3dsart_data.c */")

[System.IO.File]::WriteAllText($outFile, $sb.ToString())
Write-Output "wrote $outFile"
$notes | ForEach-Object { Write-Output "  $_" }
