# Drive Kefir Hub in the Eden emulator and take docs screenshots (see .agents/skills/update-docs/SKILL.md).
#   . tools\docs\eden.ps1
#   Start-Hub                        # launch Eden with E:\Switch\Eden\user\sdmc\switch\kefir-hub.nro
#   B A; B Down 3; B Plus            # press Switch buttons (posted to the Eden window; works unfocused)
#   Shot docs\site\en\img\<id>.png   # console frame only, 1280x720
#   Grab out.png                     # whole Eden window, for debugging
# Keys go to the Eden window by PostMessage. Never send keys with keybd_event/SendKeys/SendInput: those type
# into whatever window is in front, i.e. the user's other apps.
param()
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class EdenW {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr l);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  public static System.Collections.Generic.List<IntPtr> Children(IntPtr p) {
    var l = new System.Collections.Generic.List<IntPtr>(); EnumChildWindows(p, (h, x) => { l.Add(h); return true; }, IntPtr.Zero); return l; }
  public struct RECT { public int L, T, R, B; }
}
"@

$EdenDir = "E:\Switch\Eden"
$HubNro = "$EdenDir\user\sdmc\switch\kefir-hub.nro"

function Win { (Get-Process eden -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1).MainWindowHandle }

function Start-Hub([int]$waitSec = 25) {
  Get-Process eden -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Process -FilePath "$EdenDir\eden.exe" -ArgumentList '-g', "`"$HubNro`"" -WorkingDirectory $EdenDir
  Start-Sleep $waitSec
}

# Whole window via PrintWindow (works when other windows cover it).
function Grab([string]$path) {
  $h = Win; if (-not $h) { throw "no Eden window" }
  $r = New-Object EdenW+RECT; [EdenW]::GetWindowRect($h, [ref]$r) | Out-Null
  $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
  $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
  [EdenW]::PrintWindow($h, $dc, 2) | Out-Null; $g.ReleaseHdc($dc); $g.Dispose()
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose(); $path
}

# Console frame only: the largest child window is the render widget; the 16:9 picture is centred in it.
function Shot([string]$path) {
  $h = Win; $best = $null; $area = 0
  foreach ($c in [EdenW]::Children($h)) {
    $r = New-Object EdenW+RECT; [EdenW]::GetWindowRect($c, [ref]$r) | Out-Null
    $a = ($r.R - $r.L) * ($r.B - $r.T); if ($a -gt $area) { $area = $a; $best = $r }
  }
  $mr = New-Object EdenW+RECT; [EdenW]::GetWindowRect($h, [ref]$mr) | Out-Null
  $tmp = [System.IO.Path]::GetTempFileName() + ".png"; Grab $tmp | Out-Null
  $full = [System.Drawing.Bitmap]::FromFile($tmp)
  $cw = $best.R - $best.L; $ch = $best.B - $best.T
  $fw = [math]::Min($cw, [int]($ch * 16 / 9)); $fh = [int]($fw * 9 / 16)
  $x = $best.L - $mr.L + [int](($cw - $fw) / 2); $y = $best.T - $mr.T + [int](($ch - $fh) / 2)
  $out = New-Object System.Drawing.Bitmap 1280, 720
  $g = [System.Drawing.Graphics]::FromImage($out)
  $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $g.DrawImage($full, (New-Object System.Drawing.Rectangle 0, 0, 1280, 720), (New-Object System.Drawing.Rectangle $x, $y, $fw, $fh), [System.Drawing.GraphicsUnit]::Pixel)
  $g.Dispose(); $full.Dispose(); Remove-Item $tmp
  $out.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $out.Dispose(); $path
}

# Eden keyboard map (user\config\qt-config.ini, player_0_button_*).
$Btn = @{ A = 'C'; B = 'X'; X = 'V'; Y = 'Z'; L = 'Q'; R = 'E'; ZL = 'R'; ZR = 'T'; Plus = 'M'; Minus = 'N'; L3 = 'F'; R3 = 'G'
          Up = 'Up'; Down = 'Down'; Left = 'Left'; Right = 'Right' }

# Press a Switch button $n times: WM_KEYDOWN/WM_KEYUP posted to the Eden window.
function B([string]$button, [int]$n = 1, [int]$gapMs = 350) {
  $h = Win; if (-not $h) { throw "no Eden window" }
  $key = $Btn[$button]; if (-not $key) { throw "unknown button $button" }
  $vk = [int][System.Windows.Forms.Keys]::$key; $scan = [int][EdenW]::MapVirtualKey($vk, 0)
  $ext = if ($key -in 'Up', 'Down', 'Left', 'Right') { 1 -shl 24 } else { 0 }
  for ($i = 0; $i -lt $n; $i++) {
    [EdenW]::PostMessage($h, 0x100, [IntPtr]$vk, [IntPtr](1 -bor ($scan -shl 16) -bor $ext)) | Out-Null; Start-Sleep -Milliseconds 80
    [EdenW]::PostMessage($h, 0x101, [IntPtr]$vk, [IntPtr](1 -bor ($scan -shl 16) -bor $ext -bor (3 -shl 30))) | Out-Null
    Start-Sleep -Milliseconds $gapMs
  }
}
