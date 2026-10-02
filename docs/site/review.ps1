# Local review of the Kefir Hub docs and the reworked guide site (branch kefir-hub).
#     powershell -ExecutionPolicy Bypass -File docs\site\review.ps1
# Builds the docs, then opens two server windows (close them to stop) and the browser:
#     http://localhost:4000  guide site, branch kefir-hub (its docs links go to the local docs)
#     http://localhost:8000  Kefir Hub docs (English), /uk/ Ukrainian
# After editing docs/site/*.md run it again (or only the build line). The site reloads by itself.
param([string]$Site = "D:\git\site\switch-hub")

$repo = Resolve-Path "$PSScriptRoot\..\.."
$wsl = "/mnt/" + $repo.Path.Substring(0, 1).ToLower() + ($repo.Path.Substring(2) -replace '\\', '/')

Write-Host "Building docs..."
wsl bash -lc ". ~/.venvs/docs/bin/activate && cd '$wsl' && sh docs/site/build.sh"
if ($LASTEXITCODE -ne 0) { Write-Host "Docs build failed"; exit 1 }

Start-Process powershell -ArgumentList "-NoExit", "-Command", "python -m http.server 8000 --directory '$repo\build\docs-site'"
Start-Process powershell -WorkingDirectory $Site -ArgumentList "-NoExit", "-Command", "bundle exec jekyll serve --port 4000 --config _config.yml,_rework/_config.preview.yml"

Start-Sleep 15
Start-Process "http://localhost:4000/hbl"
Start-Process "http://localhost:8000/uk/"
