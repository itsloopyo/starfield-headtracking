#!/usr/bin/env pwsh
#Requires -Version 5.1
# Bump vendored Ultimate ASI Loader (dinput8.dll) to the latest upstream
# within the pinned range and rewrite vendor/ultimate-asi-loader/{LICENSE,README.md}.
# Manual: dev runs this when they want a fresh upstream bump, then commits the
# result. CI never refreshes.
# See ~/.claude/CLAUDE.md "Vendoring Third-Party Dependencies".
#
# Special case: Ultimate-ASI-Loader ships a DLL inside a release zip, not as a
# standalone asset. We extract dinput8.dll and vendor it directly so install.cmd
# can drop dinput8.dll straight into the game root without an unzip step.

param(
    [switch]$AcceptNewHash
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$module = Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1'
if (-not (Test-Path $module)) {
    throw "ModLoaderSetup.psm1 not found at $module. Run 'pixi run sync' to update the cameraunlock-core submodule."
}
Import-Module $module -Force

$vendorAsiDir = Join-Path $projectDir 'vendor/ultimate-asi-loader'
$vendorAsiDll = Join-Path $vendorAsiDir 'dinput8.dll'

# Pinned SHA-256 allowlist for vendored dinput8.dll. A fresh upstream bump
# must be reviewed manually: run with -AcceptNewHash, verify the new hash
# against the upstream release (signature/diff), then add it here. An
# unexpected hash aborts the refresh so a hijacked/tampered upstream
# release cannot silently land in the next commit.
$AllowedDllSha256 = @(
    '810111a7f6a6cef892877c9f7c4582ccde2d621d119891f700c5309c370508bf',
    '22fda9c71eaae02460f311bf3441638340ab591586d78f1de213c4819dcb883c'
)

if (-not (Test-Path $vendorAsiDir)) {
    New-Item -ItemType Directory -Path $vendorAsiDir -Force | Out-Null
}

# Everything upstream lands in a staging directory first. A refusal on the
# hash gate below must leave the committed vendor/ tree exactly as it was:
# extracting straight into vendor/ meant a rejected release deleted the
# vendored dinput8.dll and left the LICENSE of the release we just refused.
$tempZip   = Join-Path $env:TEMP ("asi-update-" + [IO.Path]::GetRandomFileName() + ".zip")
$stageDir  = Join-Path $env:TEMP ("asi-stage-"  + [IO.Path]::GetRandomFileName())
$stageDll  = Join-Path $stageDir 'dinput8.dll'
$stageLic  = Join-Path $stageDir 'LICENSE'
try {
    New-Item -ItemType Directory -Path $stageDir -Force | Out-Null

    Write-Host "Refreshing vendor/ultimate-asi-loader from upstream..." -ForegroundColor Cyan
    $meta = Invoke-FetchLatestLoader `
        -OutputPath $tempZip `
        -Owner 'ThirteenAG' -Repo 'Ultimate-ASI-Loader' `
        -VersionPrefix 'v9.' `
        -AssetPattern '^Ultimate-ASI-Loader_x64\.zip$'

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($tempZip)
    try {
        $dllEntry = $zip.Entries | Where-Object { $_.Name -eq 'dinput8.dll' } | Select-Object -First 1
        if (-not $dllEntry) { throw "Upstream zip $($meta.AssetName) does not contain dinput8.dll." }
        $out = [System.IO.File]::Create($stageDll)
        try { $in = $dllEntry.Open(); try { $in.CopyTo($out) } finally { $in.Dispose() } } finally { $out.Dispose() }

        $licenseEntry = $zip.Entries | Where-Object { $_.Name -match '^(license|LICENSE)(\..+)?$' -and $_.FullName -notmatch '/.+/' } | Select-Object -First 1
        if ($licenseEntry) {
            $out = [System.IO.File]::Create($stageLic)
            try { $in = $licenseEntry.Open(); try { $in.CopyTo($out) } finally { $in.Dispose() } } finally { $out.Dispose() }
        }
    } finally { $zip.Dispose() }

    if (-not (Test-Path $stageLic)) {
        $licenseUrl = "https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/$($meta.Tag)/license"
        Invoke-WebRequest -Uri $licenseUrl -OutFile $stageLic -UseBasicParsing -TimeoutSec 30 -Headers @{ "User-Agent" = "CameraUnlock-HeadTracking" }
    }

    $dllSha = (Get-FileHash -Path $stageDll -Algorithm SHA256).Hash.ToLower()
    if ($AllowedDllSha256 -notcontains $dllSha) {
        if (-not $AcceptNewHash) {
            throw @"
Refusing to vendor dinput8.dll with unrecognised SHA-256.
  Tag:      $($meta.Tag)
  Asset:    $($meta.AssetName)
  Got:      $dllSha
  Allowed:  $($AllowedDllSha256 -join ', ')
Verify the new binary against the upstream release (signature/diff), then
either re-run with -AcceptNewHash or add the hash to `$AllowedDllSha256 in
scripts/update-deps.ps1 and re-run. vendor/ is unchanged.
"@
        }
        Write-Host "  WARNING: new SHA-256 $dllSha accepted via -AcceptNewHash." -ForegroundColor Yellow
        Write-Host "  Add it to `$AllowedDllSha256 in scripts/update-deps.ps1 before committing." -ForegroundColor Yellow
    }

    Copy-Item -Path $stageDll -Destination $vendorAsiDll -Force
    Copy-Item -Path $stageLic -Destination (Join-Path $vendorAsiDir 'LICENSE') -Force

    # install.cmd records the loader version in the state file, and nothing else
    # rewrote it, so it silently described the previous build from the commit
    # after any bump onwards. It sits below the hash gate with the other writes:
    # a refused release has to leave the whole tree exactly as it was, and a
    # version bumped for a binary that was never vendored is worse than a stale
    # one, because it reads as correct.
    #
    # ReadAllText/WriteAllText round-trips the file's own CRLF line endings; an
    # install.cmd rewritten with LF silently fails on Windows.
    $installCmd = Join-Path $projectDir 'scripts/install.cmd'
    $loaderVersion = $meta.Tag -replace '^v', ''
    $installText = [System.IO.File]::ReadAllText($installCmd)
    $updatedInstall = $installText -replace 'set "ASI_LOADER_VERSION=[^"]*"', "set `"ASI_LOADER_VERSION=$loaderVersion`""
    if ($installText -notmatch 'set "ASI_LOADER_VERSION=') {
        throw "scripts/install.cmd has no ASI_LOADER_VERSION line to update - the state file would record the wrong loader build."
    }
    if ($updatedInstall -ne $installText) {
        [System.IO.File]::WriteAllText($installCmd, $updatedInstall)
        Write-Host "  scripts/install.cmd ASI_LOADER_VERSION -> $loaderVersion" -ForegroundColor Green
    }

    $readme = @(
        '# Ultimate ASI Loader (vendored)',
        '',
        'Bundled copy of Ultimate ASI Loader, the install-time source of truth.',
        'Refresh manually with `pixi run update-deps`, then commit.',
        '',
        '## Snapshot',
        '',
        '- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader',
        "- Tag: ``$($meta.Tag)``",
        "- Commit: ``$($meta.CommitSha)``",
        "- Asset: ``$($meta.AssetName)``",
        "- dinput8.dll SHA-256: ``$dllSha``",
        "- Fetched at: $($meta.FetchedAt)",
        '',
        '`dinput8.dll` is extracted from the upstream asset untouched. install.cmd copies it to',
        'the game directory as the configured ASI hook slot (dinput8.dll, winmm.dll, etc).'
    ) -join "`n"
    Set-Content -Path (Join-Path $vendorAsiDir 'README.md') -Value $readme -Encoding UTF8

    Write-Host "  tag=$($meta.Tag) sha256=$($dllSha.Substring(0,12))..." -ForegroundColor DarkGray
} finally {
    Remove-Item $tempZip -Force -ErrorAction SilentlyContinue
    Remove-Item $stageDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "vendor/ultimate-asi-loader refreshed. Review and commit." -ForegroundColor Green
