# Drives Kefir Hub on the console over MTP: queues button presses, reads the log and the open screens.
# Console side: Tools -> Settings -> "Scripted input" on (and "Logging" on for -Log); MTP running.
# Commands: sphaira/include/demo/demo_cmd.hpp (A, B, X, Y, L, R, ZL, ZR, Plus, Minus, L3, R3,
# Up, Down, Left, Right, "wait N", "dump", "lang xx").
#
#   tools\dev\hub-input.ps1 Down Down A "wait 1" dump      # press, then write state.txt
#   tools\dev\hub-input.ps1 -State                          # print /config/kefir/demo/state.txt
#   tools\dev\hub-input.ps1 -Log                            # print /config/kefir/log.txt
#   tools\dev\hub-input.ps1 Down A -State                   # press, wait, then print the state
param(
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Lines,
    [switch]$State,
    [switch]$Log,
    [string]$Device = 'Nintendo Switch',
    [string]$Storage = 'microSD card'
)

$ErrorActionPreference = 'Stop'

function Get-MtpFolder([string[]]$path) {
    $shell = New-Object -ComObject Shell.Application
    $root = $shell.NameSpace(17)
    $dev = $root.Items() | Where-Object { $_.Name -eq $Device } | Select-Object -First 1
    if (-not $dev) { throw "MTP device '$Device' not found. Is MTP running on the console?" }
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

$demo = Get-MtpFolder @('config', 'kefir', 'demo')

if ($Lines -and $Lines.Count) {
    $local = Join-Path $env:TEMP 'input.txt'
    # LF only: the console side tolerates CRLF, but one format is one less thing to wonder about.
    [IO.File]::WriteAllText($local, (($Lines -join "`n") + "`n"))
    Push-File $demo $local
    # the console reads the file within 100 ms and presses one button per 350 ms.
    Start-Sleep -Milliseconds (500 + 400 * $Lines.Count)
}

if ($State) {
    Get-Content (Pull-File $demo 'state.txt')
}

if ($Log) {
    # Windows caches MTP object sizes per session; a fresh Shell object sees the current size.
    $kefir = Get-MtpFolder @('config', 'kefir')
    Get-Content (Pull-File $kefir 'log.txt')
}
