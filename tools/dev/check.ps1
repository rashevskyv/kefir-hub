# Runs the host test loop in WSL from a Windows shell (no devkitPro needed).
#
#   powershell -ExecutionPolicy Bypass -File tools\dev\check.ps1         # tests/run.sh --quick
#   powershell -ExecutionPolicy Bypass -File tools\dev\check.ps1 -Full   # full tests/run.sh
param([switch]$Full)

$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$wslRepo = (& wsl wslpath -a ($repo -replace '\\', '/'))
if ($LASTEXITCODE -ne 0 -or -not $wslRepo) {
    Write-Error "wslpath could not translate $repo"
    exit 1
}
$wslRepo = $wslRepo.Trim()

$mode = if ($Full) { '' } else { '--quick' }
& wsl bash -lc "cd '$wslRepo' && sh tests/run.sh $mode"
exit $LASTEXITCODE
