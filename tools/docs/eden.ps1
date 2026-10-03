# Drive Kefir Hub in the Eden emulator and take docs screenshots (see .agents/skills/update-docs/SKILL.md).
#   . tools\docs\eden.ps1
#   Start-Hub                        # launch Eden with E:\Switch\Eden\user\sdmc\switch\kefir-hub.nro, wait until it runs
#   B A; B Down 3; B Plus            # press Switch buttons
#   W 2                              # let the Hub load something before the next press
#   Shot docs\site\en\img\<id>.png   # waits until every press was handled, then saves the console frame, 1280x720
#   Lang uk                          # switch the running Hub to another UI language, back on the main screen
#   Grab out.png                     # whole Eden window, for debugging
#   Restore-OwnData                  # after a manual session: put back the owner's backups Start-Hub hid
#   Set-HubLang uk                   # UI language for the next Start-Hub (writes sdmc/config/kefir/config.ini)
#   Set-HubIni demo scene <name>     # DOCS_DEMO builds: open a frozen demo scene at start (empty value = none)
#   Rec <id>; B ...; W 2; Shot ...   # record: the presses after Rec are saved as the recipe of <id> in
#                                    # docs/site/shots.json when Shot runs; tools\docs\shoot.ps1 replays them
# Input needs the DOCS_DEMO build (build/DocsDemo): presses go into sdmc/config/kefir/demo/input.txt and the Hub
# reads them itself (sphaira/source/demo/demo_input.cpp), so Eden works in the background. Eden ignores keys
# posted to its window unless it is the active one, and keybd_event/SendInput would type into the user's apps.
param()
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class EdenW {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr l);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  public static System.Collections.Generic.List<IntPtr> Children(IntPtr p) {
    var l = new System.Collections.Generic.List<IntPtr>(); EnumChildWindows(p, (h, x) => { l.Add(h); return true; }, IntPtr.Zero); return l; }
  public struct RECT { public int L, T, R, B; }
}
"@

$EdenDir = "E:\Switch\Eden"
$Sdmc = "$EdenDir\user\sdmc"
$HubNro = "$Sdmc\switch\kefir-hub.nro"
$Repo = (Resolve-Path "$PSScriptRoot\..\..").Path
$ShotsJson = "$Repo\docs\site\shots.json"
$DemoDir = "$Sdmc\config\kefir\demo"
$InputTxt = "$DemoDir\input.txt"
$ReadyFile = "$DemoDir\ready"
$Buttons = 'A', 'B', 'X', 'Y', 'L', 'R', 'ZL', 'ZR', 'Plus', 'Minus', 'L3', 'R3', 'Up', 'Down', 'Left', 'Right'

# Set one key in the Hub config (sdmc/config/kefir/config.ini) for the next Start-Hub. Empty value removes the key.
function Set-HubIni([string]$section, [string]$key, [string]$value) {
  $ini = "$Sdmc\config\kefir\config.ini"
  New-Item -ItemType Directory -Force (Split-Path $ini) | Out-Null
  $lines = [System.Collections.Generic.List[string]]::new()
  if (Test-Path $ini) { foreach ($l in Get-Content $ini) { $lines.Add($l) } }
  $sec = $lines.IndexOf("[$section]")
  if ($sec -lt 0) { if (-not $value) { return }; $lines.Add("[$section]"); $sec = $lines.Count - 1 }
  $end = $sec + 1; while ($end -lt $lines.Count -and -not $lines[$end].StartsWith('[')) { $end++ }
  for ($i = $end - 1; $i -gt $sec; $i--) { if ($lines[$i] -match "^\s*$([regex]::Escape($key))\s*=") { $lines.RemoveAt($i) } }
  if ($value) { $lines.Insert($sec + 1, "$key=$value") }
  [System.IO.File]::WriteAllLines($ini, $lines, (New-Object System.Text.UTF8Encoding $false))
}

# UI language code = the Hub translation file name (assets/romfs/i18n/<code>.json); docs languages use the same codes.
function Assert-Lang([string]$lang) {
  if (-not (Test-Path "$Repo\assets\romfs\i18n\$lang.json")) {
    throw "unknown language $lang (known: $((Get-ChildItem "$Repo\assets\romfs\i18n\*.json").BaseName -join ', '))"
  }
}

# UI language for the next Start-Hub: [config] language=<code>.
function Set-HubLang([string]$lang) { Assert-Lang $lang; Set-HubIni config language $lang }

# Queue lines for the Hub (demo_cmd.hpp). The Hub deletes input.txt when it has read it; wait for that first,
# then write a temp file and rename it so the Hub never reads half a file.
function Send([string[]]$lines, [int]$timeoutSec = 120) {
  $t = [Diagnostics.Stopwatch]::StartNew()
  while (Test-Path $InputTxt) {
    if ($t.Elapsed.TotalSeconds -gt $timeoutSec) { throw "the Hub does not read $InputTxt (not a DocsDemo build, or not running)" }
    Start-Sleep -Milliseconds 50
  }
  New-Item -ItemType Directory -Force $DemoDir | Out-Null
  [System.IO.File]::WriteAllLines("$InputTxt.tmp", $lines)
  Move-Item -Force "$InputTxt.tmp" $InputTxt
}

# Wait until the Hub has handled every queued line.
function Sync([int]$timeoutSec = 120) {
  Remove-Item $ReadyFile -ErrorAction SilentlyContinue
  Send @('ready') $timeoutSec
  $t = [Diagnostics.Stopwatch]::StartNew()
  while (-not (Test-Path $ReadyFile)) {
    if ($t.Elapsed.TotalSeconds -gt $timeoutSec) { throw "the Hub did not answer within $timeoutSec s" }
    Start-Sleep -Milliseconds 50
  }
  Remove-Item $ReadyFile
}

# Recording. Rec <id> starts a recipe; B and W append to it; Shot saves it into docs/site/shots.json.
$script:RecId = $null; $script:RecSteps = $null
function Rec([string]$id) { $script:RecId = $id; $script:RecSteps = [System.Collections.Generic.List[string]]::new() }
function W([double]$sec) {
  $s = "wait $([string]::Format([Globalization.CultureInfo]::InvariantCulture, '{0}', $sec))"
  if ($script:RecId) { $script:RecSteps.Add($s) }
  Send @($s)
}

# Press a Switch button $n times (the Hub paces presses 350 ms apart).
function B([string]$button, [int]$n = 1) {
  if ($button -cnotin $Buttons) { throw "unknown button $button (known: $($Buttons -join ', '))" }
  if ($script:RecId) { $script:RecSteps.Add($(if ($n -eq 1) { $button } else { "$button $n" })) }
  Send @(1..$n | ForEach-Object { $button })
}

# Switch the running Hub to another UI language: it rebuilds its menus on the main screen.
function Lang([string]$lang) { Assert-Lang $lang; Send @("lang $lang"); Sync }

function Read-Shots {
  if (Test-Path $ShotsJson) { Get-Content $ShotsJson -Raw -Encoding UTF8 | ConvertFrom-Json } else { [pscustomobject]@{ startup = @(); shots = [pscustomobject]@{} } }
}
function Write-Shots($data) {
  [System.IO.File]::WriteAllText($ShotsJson, ($data | ConvertTo-Json -Depth 6), (New-Object System.Text.UTF8Encoding $false))
}
function Save-Recipe([string]$id, [string[]]$steps) {
  $data = Read-Shots
  $entry = $data.shots.PSObject.Properties[$id]
  if ($entry) { $entry.Value | Add-Member -Force steps $steps } else { $data.shots | Add-Member $id ([pscustomobject]@{ steps = $steps }) }
  Write-Shots $data
  Write-Host "recipe saved: $id ($($steps.Count) steps)"
}

# The owner's own save backups on the Eden SD (real games and nicknames) must not show on docs shots. Hide-OwnData
# moves every backup folder that does not come from docs/site/fixtures to $Hidden (same relative paths);
# Restore-OwnData moves it back. Start-Hub hides, shoot.ps1 restores at the end: after a manual session run
# Restore-OwnData yourself. Nothing is deleted.
$Hidden = "$EdenDir\user\sdmc-hidden-by-docs"
$BackupRoots = 'dumps', 'DBISaves', 'switch\DBI\saves', 'JKSV', 'switch\JKSV', 'switch\Checkpoint\saves', 'Checkpoint\saves'
function Hide-OwnData {
  $n = 0
  $fixtures = "$Repo\docs\site\fixtures\sdmc"
  foreach ($root in $BackupRoots) {
    if (-not (Test-Path "$Sdmc\$root")) { continue }
    foreach ($item in Get-ChildItem -Force "$Sdmc\$root") {
      if (Test-Path "$fixtures\$root\$($item.Name)") { continue }
      $dest = "$Hidden\$root"
      New-Item -ItemType Directory -Force $dest | Out-Null
      if (Test-Path "$dest\$($item.Name)") { throw "$dest\$($item.Name) already exists: run Restore-OwnData first" }
      Move-Item -LiteralPath $item.FullName -Destination $dest
      $n++
    }
  }
  if ($n) { Write-Host "own backups hidden: $n folders -> $Hidden" }
}
function Restore-OwnData {
  if (-not (Test-Path $Hidden)) { return }
  foreach ($root in $BackupRoots) {
    if (-not (Test-Path "$Hidden\$root")) { continue }
    New-Item -ItemType Directory -Force "$Sdmc\$root" | Out-Null
    foreach ($item in Get-ChildItem -Force "$Hidden\$root") {
      if (Test-Path "$Sdmc\$root\$($item.Name)") { Write-Warning "kept hidden, name taken on the SD: $root\$($item.Name)"; continue }
      Move-Item -LiteralPath $item.FullName -Destination "$Sdmc\$root"
    }
  }
  if (-not (Get-ChildItem -Recurse -File -Force $Hidden)) { Remove-Item -Recurse -Force $Hidden }
  Write-Host "own backups restored"
}

function Win { (Get-Process eden -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1).MainWindowHandle }

# Launch Eden and wait until the Hub reads input (startup dialogs are the recipe's "startup" steps).
function Start-Hub([int]$waitSec = 90) {
  Get-Process eden -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 500
  Remove-Item $InputTxt, "$InputTxt.tmp", $ReadyFile -ErrorAction SilentlyContinue
  Hide-OwnData
  Start-Process -FilePath "$EdenDir\eden.exe" -ArgumentList '-g', "`"$HubNro`"" -WorkingDirectory $EdenDir
  Sync $waitSec
  Start-Sleep 1  # first frames after the dialogs open
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
  Sync
  Start-Sleep -Milliseconds 300  # the frame after the last press
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
  $dest = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($path)  # .NET ignores Set-Location
  New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
  $out.Save($dest, [System.Drawing.Imaging.ImageFormat]::Png); $out.Dispose()
  if ($script:RecId) { Save-Recipe $script:RecId $script:RecSteps.ToArray(); $script:RecId = $null }
  $path
}
