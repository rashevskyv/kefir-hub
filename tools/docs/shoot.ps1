# Replay the screenshot recipes in docs/site/shots.json in Eden, for any UI language.
#   powershell -ExecutionPolicy Bypass -File tools\docs\shoot.ps1 -Lang uk,en          # every recipe, two languages
#   ... -Lang uk -Only saves-list,users-options                                          # some shots
#   ... -Lang ru -Missing                                                                # only shots not taken yet
#   ... -List                                                                            # recipe status, no emulator
# Output: docs/site/<lang>/img/<id>.png (1280x720). A recipe is recorded once with eden.ps1 (Rec/B/W/Shot) and
# then works for every language, because it is a list of button presses, not of labels.
# Eden is launched once (again only when a recipe needs another demo scene). For each shot, every language in turn:
# `lang <code>` puts the running Hub on a fresh main screen in that language, the recipe is replayed, the frame is
# saved, then the next language. Eden can stay in the background. Before the run: the DOCS_DEMO build
# (build/DocsDemo/kefir-hub.nro; -Nro to override) is copied into Eden, and docs/site/fixtures/sdmc/ over the Eden sdmc.
# A recipe may name a demo scene ("scene": "<name>", DOCS_DEMO builds only); it is set as [demo] scene=<name>.
# "ini": {"config.auto_update": "2"} sets Hub config keys for that shot's launch (removed again afterwards);
# "startup": [...] replaces the shared startup steps for it. Such a shot gets its own launch.
# "crop": [x, y, w, h] keeps only that part of the 1280x720 frame (close-ups).
# "fresh": true = own launch per language (a server or transfer it starts outlives `lang`).
param([string[]]$Lang = @('en'), [string[]]$Only = @(), [switch]$Missing, [int]$Wait = 90, [switch]$List,
      [string]$Nro = '')
$ErrorActionPreference = 'Stop'
# powershell -File passes "uk,en" as one string
$Lang = @($Lang -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$Only = @($Only -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
. "$PSScriptRoot\eden.ps1"

$data = Read-Shots
$ids = @($data.shots.PSObject.Properties.Name)
if ($Only.Count) { $ids = @($ids | Where-Object { $_ -in $Only }); $Only | Where-Object { $_ -notin $ids } | ForEach-Object { Write-Warning "no recipe: $_" } }

function Status($id) {
  $e = $data.shots.$id
  if ($e.user) { 'user' } elseif ($e.web) { 'web' } elseif ($e.scene -or ($e.steps -and $e.steps.Count)) { 'recipe' } else { 'empty' }
}

if ($List) {
  foreach ($id in $ids) { '{0,-8} {1}' -f (Status $id), $id }
  return
}

function Step([string]$s) {
  $p = $s.Trim() -split '\s+'
  if ($p[0] -eq 'wait') { W ([double]::Parse($p[1], [Globalization.CultureInfo]::InvariantCulture)); return }
  if ($p.Count -gt 1) { B $p[0] ([int]$p[1]) } else { B $p[0] }
}

Get-Process eden -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep -Milliseconds 500  # it locks the .nro
if (-not $Nro) { $Nro = "$Repo\build\DocsDemo\kefir-hub.nro" }
if (Test-Path $Nro) { Copy-Item $Nro $HubNro -Force; Write-Host "hub: $Nro" } else { Write-Warning "no $Nro; using the .nro already in Eden" }

$fixtures = "$Repo\docs\site\fixtures\sdmc"
if (Test-Path $fixtures) { Copy-Item "$fixtures\*" $Sdmc -Recurse -Force }

$Lang | ForEach-Object { Assert-Lang $_ }
$taken = 0; $skipped = @(); $scene = $null; $iniSet = @()

# Fresh launch for $id in $lang: its "ini" keys ({"section.key": "value"}), scene, then the startup steps
# (a recipe's own "startup" replaces the shared one).
function Launch($id, $lang) {
  # the running Hub writes config.ini too (last_path when a menu closes): stop it before setting keys.
  Get-Process eden -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep -Milliseconds 500
  foreach ($k in $script:iniSet) { $sk = $k -split '\.', 2; Set-HubIni $sk[0] $sk[1] '' }
  $script:iniSet = @()
  $ini = $data.shots.$id.ini
  if ($ini) { foreach ($p in $ini.PSObject.Properties) { $sk = $p.Name -split '\.', 2; Set-HubIni $sk[0] $sk[1] "$($p.Value)"; $script:iniSet += $p.Name } }
  Set-HubIni demo scene "$($data.shots.$id.scene)"
  Set-HubLang $lang
  Start-Hub $Wait
  # a scene opens on top of the startup dialogs, so it needs no startup steps unless the recipe names some.
  $startup = if ($data.shots.$id.PSObject.Properties['startup']) { $data.shots.$id.startup } elseif ($data.shots.$id.scene) { @() } else { $data.startup }
  foreach ($s in @($startup)) { if ($s) { Step $s } }
}
function Take($id, $lang) {
  foreach ($s in $data.shots.$id.steps) { Step $s }
  Shot "$Repo\docs\site\$lang\img\$id.png" | Out-Null
  if ($data.shots.$id.crop) { Crop-Png "$Repo\docs\site\$lang\img\$id.png" @($data.shots.$id.crop) }
  $script:taken++; Write-Host "shot $lang/$id"
}

try {
foreach ($id in $ids) {
  $st = Status $id
  if ($st -ne 'recipe') { $skipped += "$id ($st)"; continue }
  $todo = @($Lang | Where-Object { -not ($Missing -and (Test-Path "$Repo\docs\site\$_\img\$id.png")) })
  if (-not $todo.Count) { continue }
  if ($data.shots.$id.PSObject.Properties['startup'] -or $data.shots.$id.scene -or $data.shots.$id.fresh) {
    # its screen exists only right after a launch (a scene, a startup dialog): `lang` would rebuild it away.
    foreach ($l in $todo) { Launch $id $l; Take $id $l }
    $scene = $null
    continue
  }
  # shots with the same scene and "ini" keys share one launch; `lang` switches the language in place.
  $want = "$($data.shots.$id.scene)|$(if ($data.shots.$id.ini) { $data.shots.$id.ini | ConvertTo-Json -Compress })"
  if ($want -ne $scene) { Launch $id $todo[0]; $scene = $want }
  foreach ($l in $todo) { Lang $l; Take $id $l }
}
} finally {
  Get-Process eden -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 500
  Restore-OwnData
  foreach ($k in $iniSet) { $sk = $k -split '\.', 2; Set-HubIni $sk[0] $sk[1] '' }
}
Set-HubIni demo scene ''
Write-Host "taken: $taken"
if ($skipped.Count) { Write-Host "skipped (user = console-only, web = node tools/docs/web-shot.mjs with the Hub web server on, empty = record it first):"; $skipped | ForEach-Object { "  $_" } }
Write-Host "Open every new PNG and check it matches its <!-- shot --> marker."
