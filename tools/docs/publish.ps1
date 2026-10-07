# One button: check, build and publish the Kefir Hub docs and the guide.
#
#   tools\docs\publish.cmd                       (double-click)
#   powershell -ExecutionPolicy Bypass -File tools\docs\publish.ps1 [-SkipChecks]
#
# 1. Both clones must be clean (TegraExplorer.bin ignored) and ahead of, or equal to, their remote branch.
# 2. Draft blocks in docs/site pages become prose through the LLM proxy (tools/docs/finish.py), then
#    checks: doc labels, docs build (WSL venv ~/.venvs/docs), guide links into the built docs; a finished
#    draft is committed as 'docs: finish drafts'.
# 3. Push: sphaira master -> docs (publishes https://hub.customfw.xyz/), guide kefir-hub (its workflow
#    commits docs/site/guide.ref on branch guide-sync, which rebuilds https://hub.customfw.xyz/guide/).
# 4. Wait for the Pages deploys and probe the site.
param([switch]$SkipChecks)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$guide = 'D:\git\site\switch-hub'
$site = 'https://hub.customfw.xyz'

function Run($name, [scriptblock]$body) {
    Write-Host "== $name" -ForegroundColor Cyan
    & $body
    if ($LASTEXITCODE) { throw "$name failed (exit $LASTEXITCODE)" }
}

function Ready($dir, $remote, $local) {
    $dirty = git -C $dir status --short | Where-Object { $_ -notmatch 'TegraExplorer\.bin$' }
    if ($dirty) { throw "$dir has uncommitted changes:`n$($dirty -join "`n")" }
    git -C $dir fetch origin --quiet
    git -C $dir merge-base --is-ancestor "origin/$remote" $local
    if ($LASTEXITCODE -eq 1) { throw "$dir`: origin/$remote has commits that $local lacks; merge origin/$remote into $local first" }
    if ($LASTEXITCODE) { throw "git merge-base failed in $dir" }
}

Run 'sphaira ready' { Ready $repo docs master }
Run 'guide ready' { git -C $guide checkout --quiet kefir-hub; Ready $guide kefir-hub kefir-hub }

# Draft blocks the coding agent left in the pages become prose through the LLM proxy (tools/docs/finish.py).
Run 'finish drafts' { python -I "$repo\tools\docs\finish.py" }
$finished = git -C $repo status --short docs/site

if (-not $SkipChecks) {
    $wslRepo = (& wsl wslpath -a ($repo -replace '\\', '/')).Trim()
    Run 'doc labels' { python -I "$repo\tests\test_doc_labels_contract.py" }
    Run 'docs build' { wsl bash -lc ". ~/.venvs/docs/bin/activate && cd '$wslRepo' && sh docs/site/build.sh" }
    Run 'guide links' { python -I "$repo\tools\docs\check_site_links.py" $guide }
}

if ($finished) {
    Run 'commit finished drafts' { git -C $repo add docs/site; git -C $repo commit --quiet -m 'docs: finish drafts (tools/docs/finish.py)' }
}

$docsBefore = git -C $repo rev-parse origin/docs
$guideBefore = git -C $guide rev-parse origin/kefir-hub
$t0 = [DateTime]::UtcNow
Run 'push docs' { git -C $repo push origin master:docs }
Run 'push guide' { git -C $guide push origin kefir-hub }
$docsPushed = (git -C $repo rev-parse master) -ne $docsBefore
$guidePushed = (git -C $guide rev-parse kefir-hub) -ne $guideBefore
if (-not ($docsPushed -or $guidePushed)) { Write-Host 'Nothing new to publish.' -ForegroundColor Green; exit 0 }

Write-Host '== deploy (2-4 min)' -ForegroundColor Cyan
Start-Sleep 40   # the guide push reaches kefir-hub through a second workflow; let both runs start
do {
    Start-Sleep 20
    $runs = gh run list -R rashevskyv/kefir-hub -w docs-preview -L 3 --json status,conclusion | ConvertFrom-Json
    $busy = $runs | Where-Object { $_.status -ne 'completed' }
} while ($busy -and ([DateTime]::UtcNow - $t0).TotalMinutes -lt 15)
if ($busy) { throw 'docs-preview is still running after 15 min: https://github.com/rashevskyv/kefir-hub/actions' }
if ($runs[0].conclusion -ne 'success') { throw "docs-preview $($runs[0].conclusion): https://github.com/rashevskyv/kefir-hub/actions" }

# ponytail: one probe per site part; the CDN may lag the deploy by a minute, so retry briefly.
foreach ($path in @('/', '/guide/')) {
    for ($i = 0; $i -lt 12; $i++) {
        $mod = [DateTime]::Parse((Invoke-WebRequest -Method Head -UseBasicParsing "$site$path").Headers['Last-Modified']).ToUniversalTime()
        if ($mod -ge $t0) { break }
        Start-Sleep 10
    }
    if ($mod -lt $t0) { throw "$site$path still serves the build of $mod UTC" }
    Write-Host "$site$path  updated $mod UTC" -ForegroundColor Green
}
