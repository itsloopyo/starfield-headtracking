#!/usr/bin/env pwsh
#Requires -Version 5.1
# Package release ZIPs for Starfield Head Tracking.
# Produces two archives in release/:
#   - StarfieldHeadTracking-v<version>-installer.zip (GitHub Releases)
#       launcher-manifest.json + install.cmd + uninstall.cmd + plugins/
#       + vendor/ultimate-asi-loader/
#       + shared/ (find-game.ps1 + GamePathDetection.psm1 + games.json)
#       + docs
#   - StarfieldHeadTracking-v<version>-nexus.zip (Nexus Mods)
#       StarfieldHeadTracking.asi at the archive root, alongside
#       README/CHANGELOG/THIRD-PARTY-NOTICES/LICENSE. Users drop the .asi
#       next to Starfield.exe. No HeadTracking.ini - the mod self-generates it
#       on first launch, so bundling it would clobber user config on update.
#       Nexus users manage their own ASI loader.
#
# Vendoring is refreshed manually by the dev via 'pixi run update-deps'
# (scripts/update-deps.ps1) and committed under vendor/. CI never refreshes;
# this script consumes whatever is committed.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectDir "cameraunlock-core/powershell/ReleaseWorkflow.psm1") -Force

# cameraunlock-core carries its own MIT grant, separate from this mod's LICENSE
# even though both name itsloopyo, and it is compiled into the .asi, so MIT
# wants that notice travelling with the binary in BOTH ZIPs. A shared copyright
# holder does not merge the two grants: the core is published on its own terms
# to its own consumers, and shipping only this mod's LICENSE would be asserting
# it covers code it does not. Defined here
# rather than taken from the submodule on purpose: a helper added to core does
# not reach this mod until its submodule pointer moves, and a licence
# obligation cannot wait on that.
function Copy-CoreLicense {
    param([Parameter(Mandatory)][string]$StagingDir)

    $src = Join-Path $projectDir "cameraunlock-core/LICENSE"
    if (-not (Test-Path $src)) {
        throw "cameraunlock-core/LICENSE not found at: $src. Run 'git submodule update --init' - the core licence must ship in every release ZIP."
    }

    $destDir = Join-Path $StagingDir "licenses"
    New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    Copy-Item $src -Destination (Join-Path $destDir "cameraunlock-core-LICENSE.txt") -Force
    Write-Host "  licenses/cameraunlock-core-LICENSE.txt" -ForegroundColor Green
}

# CMakeLists.txt is the canonical version source, and release.yml reads it the
# same way (version-source: cmake) to check the tag against the tree.
$version  = Get-ProjectVersion -Source cmake -Path (Join-Path $projectDir "CMakeLists.txt")
$modName  = 'StarfieldHeadTracking'

Write-Host "=== $modName - Package Release ===" -ForegroundColor Magenta
Write-Host ""
Write-Host "Version: $version" -ForegroundColor Cyan
Write-Host ""

$releaseDir = Join-Path $projectDir "release"
if (-not (Test-Path $releaseDir)) {
    New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
}

# --- Required source artifacts ------------------------------------------------

$asiPath = Join-Path $projectDir "bin/Release/$modName.asi"
if (-not (Test-Path $asiPath)) {
    throw "$modName.asi not found at: $asiPath. Run 'pixi run build-release' first."
}

$iniPath = Join-Path $projectDir "HeadTracking.ini"
if (-not (Test-Path $iniPath)) { throw "HeadTracking.ini not found at: $iniPath" }

$scriptsDir = Join-Path $projectDir "scripts"
foreach ($script in @("install.cmd", "uninstall.cmd")) {
    $p = Join-Path $scriptsDir $script
    if (-not (Test-Path $p)) { throw "Required script not found: $p" }
}

$vendorDir = Join-Path $projectDir "vendor/ultimate-asi-loader"
foreach ($f in @('dinput8.dll', 'LICENSE', 'README.md')) {
    $p = Join-Path $vendorDir $f
    if (-not (Test-Path $p)) {
        throw "vendor/ultimate-asi-loader/$f missing. Run 'pixi run update-deps' to populate."
    }
}

# --- Installer ZIP ------------------------------------------------------------

Write-Host "--- Installer ZIP ---" -ForegroundColor Yellow
Write-Host ""

$stagingInstaller = Join-Path $releaseDir "staging-installer"
if (Test-Path $stagingInstaller) { Remove-Item -Recurse -Force $stagingInstaller }
New-Item -ItemType Directory -Path $stagingInstaller -Force | Out-Null

foreach ($script in @("install.cmd", "uninstall.cmd")) {
    Copy-Item (Join-Path $scriptsDir $script) -Destination $stagingInstaller -Force
    Write-Host "  $script" -ForegroundColor Green
}

# Canonical launcher manifest (AGENTS.md: launcher-manifest.json, snake_case v2
# schema). Stamped to the release version and dropped at the installer ZIP root
# so the launcher can ingest this package's metadata (mod_info / files / loader /
# dependencies / runtime_requirements) without parsing install.cmd. delivery_mode
# is "manifest": the launcher deploys the two files declared in the manifest
# itself, and install.cmd is there for the manual route rather than for it.
$manifestSource = Join-Path $projectDir "launcher-manifest.json"
if (-not (Test-Path $manifestSource)) { throw "launcher-manifest.json not found at: $manifestSource" }

# loader.seed is a base64 copy of HeadTracking.ini, and it is the config a
# launcher-deployed user actually gets. The committed manifest is the
# authoritative copy of it: reviewable, diffable and in git, where the blob
# inside the ZIP is a build product. Refreshing the blob from disk here would
# ship a correct ZIP over a stale committed file, so drift fails the build and
# gets re-stamped in a commit instead.
Assert-ManifestSeedsMatchShipped -ManifestPath $manifestSource -ProjectRoot $projectDir

$launcherManifest = Get-Content $manifestSource -Raw | ConvertFrom-Json
$launcherManifest.mod_info.version = $version
$launcherManifest | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $stagingInstaller "launcher-manifest.json") -NoNewline
Write-Host "  launcher-manifest.json" -ForegroundColor Green

$pluginsDir = Join-Path $stagingInstaller "plugins"
New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null
Copy-Item $asiPath -Destination $pluginsDir -Force
Write-Host "  plugins/$modName.asi" -ForegroundColor Green
Copy-Item $iniPath -Destination $pluginsDir -Force
Write-Host "  plugins/HeadTracking.ini" -ForegroundColor Green

$vendorDest = Join-Path $stagingInstaller "vendor/ultimate-asi-loader"
New-Item -ItemType Directory -Path $vendorDest -Force | Out-Null
foreach ($f in @('dinput8.dll', 'LICENSE', 'README.md')) {
    Copy-Item (Join-Path $vendorDir $f) -Destination $vendorDest -Force
    Write-Host "  vendor/ultimate-asi-loader/$f" -ForegroundColor Green
}

# install.cmd expects shared/find-game.ps1 + GamePathDetection.psm1 + games.json
Copy-SharedBundle -StagingDir $stagingInstaller

# LICENSE and THIRD-PARTY-NOTICES.md carry the notices for everything linked
# into the .asi, so a missing one is a licence violation rather than a cosmetic
# gap. Throw instead of skipping quietly: a guarded copy turns that violation
# into a green build.
foreach ($doc in @("README.md", "CHANGELOG.md", "THIRD-PARTY-NOTICES.md", "LICENSE")) {
    $p = Join-Path $projectDir $doc
    if (-not (Test-Path $p)) { throw "Required doc for installer ZIP not found: $p" }
    Copy-Item $p -Destination $stagingInstaller -Force
    Write-Host "  $doc" -ForegroundColor Green
}

Copy-CoreLicense -StagingDir $stagingInstaller

$installerZip = Join-Path $releaseDir "$modName-v$version-installer.zip"
if (Test-Path $installerZip) { Remove-Item $installerZip -Force }

Write-Host ""
Write-Host "Creating installer ZIP..." -ForegroundColor Cyan
# tar.exe (bsdtar), not Compress-Archive - see the note on the Nexus ZIP below.
# Windows PowerShell 5.1 writes backslash separators, so a strict extractor
# unpacks "shared\find-game.ps1" as one flat filename, and install.cmd
# then dies with "find-game.ps1 not found" on every run.
$tarExe = Join-Path $env:SystemRoot "System32\tar.exe"
Push-Location $stagingInstaller
try {
    & $tarExe -a -c -f $installerZip *
    if ($LASTEXITCODE -ne 0) { throw "tar.exe failed to create the installer ZIP (exit $LASTEXITCODE)" }
} finally { Pop-Location }
Remove-Item -Recurse -Force $stagingInstaller

$installerKB = (Get-Item $installerZip).Length / 1KB
Write-Host ("  $installerZip ({0:N1} KB)" -f $installerKB) -ForegroundColor Green

# --- Nexus ZIP ----------------------------------------------------------------

Write-Host ""
Write-Host "--- Nexus ZIP ---" -ForegroundColor Yellow
Write-Host ""

$stagingNexus = Join-Path $releaseDir "staging-nexus"
if (Test-Path $stagingNexus) { Remove-Item -Recurse -Force $stagingNexus }
New-Item -ItemType Directory -Path $stagingNexus -Force | Out-Null

Copy-Item $asiPath -Destination $stagingNexus -Force
Write-Host "  $modName.asi" -ForegroundColor Green

# HeadTracking.ini is deliberately NOT shipped: the mod creates it with
# defaults on first launch if absent, and converts an older one in place, so
# bundling it would put the stamped default over the user's tuned config every
# time they update the mod through Nexus, and no conversion would run.
#
# Docs sit at the archive root (informational, not deployed to the game
# folder). THIRD-PARTY-NOTICES travels with the binary for attribution of the
# statically-linked libraries (MinHook, inih).
foreach ($doc in @("README.md", "CHANGELOG.md", "THIRD-PARTY-NOTICES.md", "LICENSE")) {
    $p = Join-Path $projectDir $doc
    if (-not (Test-Path $p)) { throw "Required doc for Nexus ZIP not found: $p" }
    Copy-Item $p -Destination $stagingNexus -Force
    Write-Host "  $doc" -ForegroundColor Green
}

Copy-CoreLicense -StagingDir $stagingNexus

$nexusZip = Join-Path $releaseDir "$modName-v$version-nexus.zip"
if (Test-Path $nexusZip) { Remove-Item $nexusZip -Force }

Write-Host ""
Write-Host "Creating nexus ZIP..." -ForegroundColor Cyan
# Use tar.exe (bsdtar) instead of Compress-Archive. Windows PowerShell 5.1's
# Compress-Archive writes backslash path separators into the zip, which is not
# spec-compliant, and stricter extractors then treat a path as a literal
# filename rather than a folder. bsdtar writes forward slashes.
#
# This archive has no subdirectories - the .asi sits at its root - because the
# loader only scans the directory holding Starfield.exe. Whether a mod manager
# deploys it there is NOT settled: the only Vortex extension readable on the
# build machine was the bundled game stub, and the full Starfield extension that
# actually supplies queryModPath is downloaded on first manage. Read it before
# the first Nexus upload. If its deploy root turns out to be Data, this mod is
# installer-only and this whole stage comes out.
Push-Location $stagingNexus
try {
    & $tarExe -a -c -f $nexusZip *
    if ($LASTEXITCODE -ne 0) { throw "tar.exe failed to create the Nexus ZIP (exit $LASTEXITCODE)" }
} finally { Pop-Location }
Remove-Item -Recurse -Force $stagingNexus

$nexusKB = (Get-Item $nexusZip).Length / 1KB
Write-Host ("  $nexusZip ({0:N1} KB)" -f $nexusKB) -ForegroundColor Green

# --- Summary ------------------------------------------------------------------

Write-Host ""
Write-Host "=== Package Complete ===" -ForegroundColor Magenta
Write-Host ""
Write-Host ("Installer: $installerZip ({0:N1} KB)" -f $installerKB) -ForegroundColor Green
Write-Host ("Nexus:     $nexusZip ({0:N1} KB)" -f $nexusKB) -ForegroundColor Green

Write-Output $installerZip
Write-Output $nexusZip
