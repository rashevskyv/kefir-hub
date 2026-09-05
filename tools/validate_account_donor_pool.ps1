# Safe non-secret validator for embedded account donor pool
# Standard PowerShell/.NET only.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Fail-Validation {
    param(
        [string]$DonorLabel,
        [string]$Category
    )
    if ([string]::IsNullOrEmpty($DonorLabel)) {
        Write-Error ("FAIL: " + $Category)
    } else {
        Write-Error ("FAIL [" + $DonorLabel + "]: " + $Category)
    }
    exit 1
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$accountLinkDir = Join-Path $repoRoot 'assets/romfs/account_link'
$poolManifestPath = Join-Path $accountLinkDir 'pool.txt'

if (-not (Test-Path $poolManifestPath)) {
    Fail-Validation '' 'pool.txt missing'
}

$poolContent = [System.IO.File]::ReadAllText($poolManifestPath)
$poolLines = $poolContent -split "`r?`n"
$poolKv = @{}

foreach ($line in $poolLines) {
    $trimmed = $line.Trim()
    if ($trimmed.Length -eq 0 -or $trimmed.StartsWith('#')) {
        continue
    }
    $eqIdx = $trimmed.IndexOf('=')
    if ($eqIdx -gt 0) {
        $k = $trimmed.Substring(0, $eqIdx).Trim()
        $v = $trimmed.Substring($eqIdx + 1).Trim()
        $poolKv[$k] = $v
    }
}

if ($poolKv['format'] -ne 'kefir_account_pool') {
    Fail-Validation '' 'invalid pool format'
}
if ($poolKv['version'] -ne '1') {
    Fail-Validation '' 'invalid pool version'
}
if (-not $poolKv.ContainsKey('donors')) {
    Fail-Validation '' 'missing donors entry in pool manifest'
}

$rawDonors = $poolKv['donors'] -split ','
$donors = @()
foreach ($d in $rawDonors) {
    $item = $d.Trim()
    if ($item.Length -gt 0) {
        $donors += $item
    }
}

if ($donors.Count -ne 9) {
    Fail-Validation '' ('unexpected donor count: ' + $donors.Count + ' (expected 9)')
}

$seenNas = New-Object 'System.Collections.Generic.HashSet[UInt64]'

for ($idx = 0; $idx -lt $donors.Count; $idx++) {
    $donorRel = $donors[$idx]
    $label = ('donor_' + ('{0:d2}' -f $idx) + ' (' + $donorRel + ')')

    # Path safety checks
    if ($donorRel.StartsWith('/') -or $donorRel.Contains(':') -or $donorRel.Contains('\') -or $donorRel.Contains('..')) {
        Fail-Validation $label 'unsafe donor path'
    }

    $donorDir = if ($donorRel -eq '.') { $accountLinkDir } else { Join-Path $accountLinkDir $donorRel }
    if (-not (Test-Path $donorDir)) {
        Fail-Validation $label 'directory missing'
    }

    # Manifest checks
    $manifestPath = Join-Path $donorDir 'manifest.txt'
    if (-not (Test-Path $manifestPath)) {
        Fail-Validation $label 'manifest.txt missing'
    }

    $mContent = [System.IO.File]::ReadAllText($manifestPath)
    $mLines = $mContent -split "`r?`n"
    $mKv = @{}
    foreach ($line in $mLines) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith('#')) {
            continue
        }
        $eq = $trimmed.IndexOf('=')
        if ($eq -gt 0) {
            $mKv[$trimmed.Substring(0, $eq).Trim()] = $trimmed.Substring($eq + 1).Trim()
        }
    }

    if ($mKv['format'] -ne 'kefir_account_link') {
        Fail-Validation $label 'manifest format mismatch'
    }
    if ($mKv['version'] -ne '3') {
        Fail-Validation $label 'manifest version mismatch'
    }
    if ($mKv['romfs'] -ne 'true') {
        Fail-Validation $label 'manifest romfs flag mismatch'
    }
    if ($mKv['system_save'] -ne '8000000000000010') {
        Fail-Validation $label 'manifest system_save mismatch'
    }
    if ($mKv['idgen_0011_included'] -ne 'false') {
        Fail-Validation $label 'manifest idgen_0011 mismatch'
    }
    if ($mKv['baas_file'] -ne 'baas/link.dat') {
        Fail-Validation $label 'manifest baas_file mismatch'
    }

    $manifestNasHex = $mKv['nintendo_account_id']
    if ([string]::IsNullOrEmpty($manifestNasHex) -or $manifestNasHex.Length -ne 16) {
        Fail-Validation $label 'invalid manifest nintendo_account_id format'
    }

    # BAAS checks
    $baasPath = Join-Path $donorDir 'baas/link.dat'
    if (-not (Test-Path $baasPath)) {
        Fail-Validation $label 'baas/link.dat missing'
    }
    $baasBytes = [System.IO.File]::ReadAllBytes($baasPath)
    if ($baasBytes.Length -ne 80) {
        Fail-Validation $label 'baas payload size is not 80 bytes'
    }

    $nasId = [System.BitConverter]::ToUInt64($baasBytes, 16)
    if ($nasId -eq 0) {
        Fail-Validation $label 'baas nas_id is zero'
    }

    $nasHex = ('{0:x16}' -f $nasId).ToLowerInvariant()
    if ($manifestNasHex.ToLowerInvariant() -ne $nasHex) {
        Fail-Validation $label 'manifest nas_id mismatch with baas payload'
    }

    if (-not $seenNas.Add($nasId)) {
        Fail-Validation $label 'duplicate nas identity across pool'
    }

    # BAAS directory must contain only link.dat
    $baasDir = Join-Path $donorDir 'baas'
    $baasFiles = @(Get-ChildItem -Path $baasDir -File)
    if ($baasFiles.Count -ne 1 -or $baasFiles[0].Name -ne 'link.dat') {
        Fail-Validation $label 'unexpected extra files in baas directory'
    }

    # NAS directory checks
    $nasDir = Join-Path $donorDir 'nas'
    if (-not (Test-Path $nasDir)) {
        Fail-Validation $label 'nas directory missing'
    }

    # Verify declared vs present files
    $rawNasFiles = $mKv['nas_files'] -split ','
    $declaredNas = @()
    foreach ($nf in $rawNasFiles) {
        $trimmed = $nf.Trim()
        if ($trimmed.Length -gt 0) {
            $declaredNas += $trimmed
        }
    }
    if ($declaredNas.Count -ne 4) {
        Fail-Validation $label 'declared nas_files count is not 4'
    }

    $actualNasFiles = @(Get-ChildItem -Path $nasDir -File)
    if ($actualNasFiles.Count -ne 4) {
        Fail-Validation $label 'nas directory file count is not 4'
    }

    # Invariant: forbidden optional files or extraneous files
    $forbiddenSuffixes = @('_aux.dat', '_op2membership.dat', '.tsv', '.jpg', '.jpeg', '.png')
    foreach ($af in $actualNasFiles) {
        foreach ($suf in $forbiddenSuffixes) {
            if ($af.Name.EndsWith($suf, [System.StringComparison]::OrdinalIgnoreCase)) {
                Fail-Validation $label 'forbidden file type present in donor directory'
            }
        }
        if (-not $af.Name.StartsWith($nasHex, [System.StringComparison]::OrdinalIgnoreCase)) {
            Fail-Validation $label 'nas filename does not match nas identity prefix'
        }
    }

    # Exactly four core file kinds
    $expectedDat = $nasHex + '.dat'
    $expectedUser = $nasHex + '_user.json'
    $expectedIdTok = $nasHex + '_id.token'
    $expectedRefTok = $nasHex + '_refresh.token'

    $corePaths = @($expectedDat, $expectedUser, $expectedIdTok, $expectedRefTok)
    foreach ($cp in $corePaths) {
        $p = Join-Path $nasDir $cp
        if (-not (Test-Path $p)) {
            Fail-Validation $label 'expected core nas file missing'
        }
        $info = Get-Item $p
        if ($info.Length -eq 0) {
            Fail-Validation $label 'core nas file is empty'
        }
    }

    # Validate user JSON id property
    $userJsonContent = [System.IO.File]::ReadAllText((Join-Path $nasDir $expectedUser))
    try {
        $userObj = $userJsonContent | ConvertFrom-Json
    } catch {
        Fail-Validation $label 'user JSON parse error'
    }
    if ([string]::IsNullOrEmpty($userObj.id) -or $userObj.id.ToLowerInvariant() -ne $nasHex) {
        Fail-Validation $label 'user JSON id mismatch'
    }

    # Validate token structural JWT and decoded sub
    foreach ($tokName in @($expectedIdTok, $expectedRefTok)) {
        $tokContent = ([System.IO.File]::ReadAllText((Join-Path $nasDir $tokName))).Trim()
        $tokParts = $tokContent.Split('.')
        if ($tokParts.Length -ne 3) {
            Fail-Validation $label 'token is not a three-part JWT'
        }
        $payloadB64 = $tokParts[1].Replace('-', '+').Replace('_', '/')
        switch ($payloadB64.Length % 4) {
            2 { $payloadB64 += '==' }
            3 { $payloadB64 += '=' }
        }
        try {
            $payloadBytes = [System.Convert]::FromBase64String($payloadB64)
            $payloadJson = [System.Text.Encoding]::UTF8.GetString($payloadBytes)
            $payloadObj = $payloadJson | ConvertFrom-Json
        } catch {
            Fail-Validation $label 'token payload decode/parse error'
        }
        if ([string]::IsNullOrEmpty($payloadObj.sub) -or $payloadObj.sub.ToLowerInvariant() -ne $nasHex) {
            Fail-Validation $label 'token decoded sub mismatch'
        }
    }

    # Check that donor directory root only has manifest.txt (and for '.', pool.txt and donors/ subdir)
    $rootFiles = @(Get-ChildItem -Path $donorDir -File)
    if ($donorRel -eq '.') {
        $allowedRootFiles = @('manifest.txt', 'pool.txt')
        foreach ($rf in $rootFiles) {
            if ($allowedRootFiles -notcontains $rf.Name) {
                Fail-Validation $label 'extraneous file in root donor directory'
            }
        }
    } else {
        if ($rootFiles.Count -ne 1 -or $rootFiles[0].Name -ne 'manifest.txt') {
            Fail-Validation $label 'extraneous file in donor directory'
        }
    }
}

Write-Host ('PASS: ' + $seenNas.Count + ' unique donor packages validated')
exit 0
