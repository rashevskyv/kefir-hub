# Drives Kefir Hub on the console: queues button presses, reads the log and the open screens.
# Console side: Tools -> Settings -> "Scripted input" on (and "Logging" on for -Log); MTP or FTP running.
# Commands: sphaira/include/demo/demo_cmd.hpp (A, B, X, Y, L, R, ZL, ZR, Plus, Minus, L3, R3,
# Up, Down, Left, Right, "wait N", "dump", "shot", "lang xx").
#
#   tools\dev\hub-input.ps1 Down Down A "wait 1" dump      # press, then write state.txt (MTP)
#   tools\dev\hub-input.ps1 -State                          # print /config/kefir/demo/state.txt
#   tools\dev\hub-input.ps1 -Log                            # print /config/kefir/log.txt
#   tools\dev\hub-input.ps1 -Ftp 192.168.50.69 Down A -State  # same over the Hub FTP server (port 5000)
#   tools\dev\hub-input.ps1 -Ftp 192.168.50.69 A "wait 1" shot -Shot out.jpg  # screen as JPEG on the PC
[CmdletBinding(PositionalBinding = $false)]  # bare words are commands, never -Device / -Storage values
param(
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)][string[]]$Lines,
    [switch]$State,
    [switch]$Log,
    [string]$Shot = '',
    [string]$Ftp = '',
    [string]$Device = 'Nintendo Switch',
    [string]$Storage = 'microSD card'
)

$ErrorActionPreference = 'Stop'

# `wait 2` typed without quotes arrives as two words: join "wait" with the number after it (also "lang uk").
$joined = @()
for ($i = 0; $i -lt @($Lines).Count; $i++) {
    if (($Lines[$i] -in 'wait', 'lang') -and $i + 1 -lt @($Lines).Count) { $joined += "$($Lines[$i]) $($Lines[$i + 1])"; $i++ }
    else { $joined += $Lines[$i] }
}
$Lines = $joined

# ---- MTP transport (Shell COM) ----
function Get-MtpFolder([string[]]$path) {
    $shell = New-Object -ComObject Shell.Application
    $root = $shell.NameSpace(17)
    $dev = $root.Items() | Where-Object { $_.Name -eq $Device } | Select-Object -First 1
    if (-not $dev) { throw "MTP device '$Device' not found. Is MTP running on the console? (-Ftp <ip> uses FTP instead)" }
    $folder = ($dev.GetFolder.Items() | Where-Object { $_.Name -eq $Storage } | Select-Object -First 1)
    if (-not $folder) { throw "storage '$Storage' not found" }
    $folder = $folder.GetFolder
    foreach ($part in $path) {
        $next = $folder.Items() | Where-Object { $_.Name -eq $part } | Select-Object -First 1
        if (-not $next) { throw "folder '$part' not found on the card (open Kefir Hub once with Scripted input on)" }
        $folder = $next.GetFolder
    }
    return $folder
}

function Push-File([object]$folder, [string]$local) {
    $name = Split-Path $local -Leaf
    $old = $folder.Items() | Where-Object { $_.Name -eq $name }
    if ($old) { $old | ForEach-Object { $_.InvokeVerb('delete') } }
    $folder.CopyHere((Get-Item $local).FullName, 16 + 4 + 1024)
    for ($i = 0; $i -lt 50; $i++) {
        Start-Sleep -Milliseconds 100
        if ($folder.Items() | Where-Object { $_.Name -eq $name }) { return }
    }
    throw "copy of $name did not show up"
}

function Pull-File([object]$folder, [string]$name) {
    $item = $folder.Items() | Where-Object { $_.Name -eq $name } | Select-Object -First 1
    if (-not $item) { throw "$name not found on the card" }
    $tmp = Join-Path $env:TEMP ("hub-input-" + [guid]::NewGuid().ToString('n'))
    New-Item -ItemType Directory -Path $tmp | Out-Null
    $shell = New-Object -ComObject Shell.Application
    $shell.NameSpace($tmp).CopyHere($item, 16 + 4 + 1024)
    $dst = Join-Path $tmp $name
    for ($i = 0; $i -lt 100; $i++) {
        Start-Sleep -Milliseconds 100
        if (Test-Path $dst) { break }
    }
    if (-not (Test-Path $dst)) { throw "copy of $name back to the PC failed" }
    return $dst
}

# ---- FTP transport (Hub FTP server, anonymous, port 5000) ----
function Ftp-Url([string]$path) {
    $hostport = if ($Ftp -match ':') { $Ftp } else { "$Ftp`:5000" }
    return "ftp://$hostport$path"
}

function Ftp-Push([string]$local, [string]$remote) {
    & curl.exe -s -m 30 --ftp-create-dirs -T $local (Ftp-Url $remote)
    if ($LASTEXITCODE) { throw "FTP upload of $remote failed (curl $LASTEXITCODE)" }
}

function Ftp-Pull([string]$remote) {
    $dst = Join-Path $env:TEMP ("hub-input-" + [guid]::NewGuid().ToString('n') + '-' + (Split-Path $remote -Leaf))
    & curl.exe -s -m 60 (Ftp-Url $remote) -o $dst
    if ($LASTEXITCODE) { throw "FTP download of $remote failed (curl $LASTEXITCODE)" }
    return $dst
}

if ($Lines -and $Lines.Count) {
    $local = Join-Path $env:TEMP 'input.txt'
    # LF only: the console side tolerates CRLF, but one format is one less thing to wonder about.
    [IO.File]::WriteAllText($local, (($Lines -join "`n") + "`n"))
    if ($Ftp) { Ftp-Push $local '/config/kefir/demo/input.txt' }
    else { Push-File (Get-MtpFolder @('config', 'kefir', 'demo')) $local }
    # the console reads the file within 100 ms, presses one button per 350 ms and honours every "wait N";
    # state.txt and shot.jpg are rewritten only after the whole queue, so wait for all of it.
    $waits = ($Lines | Where-Object { $_ -match '^\s*wait\s+([\d.]+)\s*$' } | ForEach-Object { [double]$Matches[1] } | Measure-Object -Sum).Sum
    Start-Sleep -Milliseconds ([int](1000 + 400 * $Lines.Count + 1000 * $waits))
}

if ($State) {
    if ($Ftp) { Get-Content (Ftp-Pull '/config/kefir/demo/state.txt') }
    else { Get-Content (Pull-File (Get-MtpFolder @('config', 'kefir', 'demo')) 'state.txt') }
}

if ($Shot) {
    $src = if ($Ftp) { Ftp-Pull '/config/kefir/demo/shot.jpg' } else { Pull-File (Get-MtpFolder @('config', 'kefir', 'demo')) 'shot.jpg' }
    Copy-Item $src $Shot -Force
    Write-Output "shot: $((Resolve-Path $Shot).Path)"
}

if ($Log) {
    if ($Ftp) { Get-Content (Ftp-Pull '/config/kefir/log.txt') }
    else {
        # Windows caches MTP object sizes per session; a fresh Shell object sees the current size.
        Get-Content (Pull-File (Get-MtpFolder @('config', 'kefir')) 'log.txt')
    }
}
