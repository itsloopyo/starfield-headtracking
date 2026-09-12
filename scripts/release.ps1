#!/usr/bin/env pwsh
#Requires -Version 5.1
# Automated release workflow for Starfield Head Tracking.
# Steps (per ~/.claude/CLAUDE.md "Build & Release"):
#   1. Validate <version> (semver)
#   2. Verify main branch, clean tree, tag unused
#   3. Generate CHANGELOG.md from commits since the last tag
#   4. Update version in CMakeLists.txt (canonical), install.cmd MOD_VERSION,
#      pixi.toml, src/core/constants.h, launcher-manifest.json
#   5. pixi run test, then pixi run package (abort on failure)
#   6. Commit "Release v<version>"
#   7. Create annotated tag v<version>
#   8. Push commits + tag (CI release workflow picks it up)
# Never destructive: refuses to run on a dirty tree or existing tag.

param(
    [Parameter(Position=0)]
    [string]$Version = "",
    # Ship a release even when there are no user-facing commits since the
    # last tag (writes a maintenance changelog entry instead of aborting).
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir    = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir   = Split-Path -Parent $scriptDir
$installCmd   = Join-Path $scriptDir "install.cmd"
$cmakePath    = Join-Path $projectDir "CMakeLists.txt"
$pixiPath     = Join-Path $projectDir "pixi.toml"
$constantsPath = Join-Path $projectDir "src/core/constants.h"
$manifestJsonPath = Join-Path $projectDir "launcher-manifest.json"

Import-Module (Join-Path $projectDir "cameraunlock-core/powershell/ReleaseWorkflow.psm1") -Force

# Mirrors New-ChangelogFromCommits' insertion so a -Force maintenance entry
# lands in the same place with the same shape.
function Add-MaintenanceChangelogEntry {
    param([string]$Path, [string]$NewVersion)
    $date = Get-Date -Format 'yyyy-MM-dd'
    $entry = "## [$NewVersion] - $date`n`n### Changed`n`n- Maintenance release (no user-facing changes).`n`n"
    $changelog = Get-Content $Path -Raw
    if ($changelog -match '(?s)(# Changelog.*?)(## \[)') {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$entry"
    } else {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n)', "`$1$entry"
    }
    $changelog = $changelog.TrimEnd() + "`n"
    Set-Content $Path $changelog -NoNewline
}

Write-Host "=== Starfield Head Tracking Release ===" -ForegroundColor Cyan
Write-Host ""

# CMakeLists.txt is the canonical version source; release.yml reads it the same
# way (version-source: cmake) to check the pushed tag against the tree.
$currentVersion = Get-ProjectVersion -Source cmake -Path $cmakePath

if ([string]::IsNullOrWhiteSpace($Version)) {
    Write-Host "Current version: " -NoNewline -ForegroundColor Yellow
    Write-Host $currentVersion -ForegroundColor White
    Write-Host ""
    Write-Host "Usage: " -NoNewline -ForegroundColor Yellow
    Write-Host "pixi run release <major|minor|patch|nightly|X.Y.Z>" -ForegroundColor White
    exit 0
}

if ($Version -eq 'nightly') {
    # release-nightly.ps1 never calls exit, so $LASTEXITCODE after it is whatever
    # the last native command in the session left behind. $ErrorActionPreference
    # is Stop, so a failure throws rather than returning a code.
    & (Join-Path $scriptDir 'release-nightly.ps1')
    exit 0
}

# Step 1: resolve major/minor/patch into a concrete version (or accept literal X.Y.Z)
try {
    $Version = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $currentVersion
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

$tagName = "v$Version"

# Step 2: git preconditions
$currentBranch = git rev-parse --abbrev-ref HEAD
if ($currentBranch -ne "main") {
    Write-Host "Error: Must be on 'main' branch (currently on '$currentBranch')" -ForegroundColor Red
    exit 1
}

if (-not (Test-CleanGitStatus)) {
    Write-Host "Error: Working directory has uncommitted changes" -ForegroundColor Red
    exit 1
}

if (Test-GitTagExists $tagName) {
    Write-Host "Error: Tag '$tagName' already exists" -ForegroundColor Red
    exit 1
}

Write-Host "Current version: $currentVersion" -ForegroundColor Gray
Write-Host "New version:     $Version" -ForegroundColor Green
Write-Host ""

# THIRD-PARTY-NOTICES.md names the cameraunlock-core commit compiled into the
# release ZIPs, and bumping the submodule does not touch it. Packaging refuses
# to ship that mismatch, so a bump with no notices edit stopped the release
# here, or in CI once the tag had already been pushed. Re-sync it and let this
# release carry the correction.
#
# This runs AFTER the branch, clean-tree, tag and version gates. It writes a
# commit, and running it first meant `pixi run release` with no argument - the
# documented way to print the current version - could land a chore commit on
# whatever branch the developer happened to be on and then exit 0.
# No check here that THIRD-PARTY-NOTICES.md is unmodified: Test-CleanGitStatus
# above refuses any dirty file at all, so it cannot be.
#
# sync-core-notices.ps1 signals failure with `exit 1`, not a throw, so the exit
# code is what has to be tested. A try/catch alone never fires and walks past a
# repo whose notices record no core commit.
& (Join-Path $projectDir 'cameraunlock-core\scripts\sync-core-notices.ps1') -Repo $projectDir
if ($LASTEXITCODE -ne 0) { throw "sync-core-notices.ps1 exited $LASTEXITCODE - fix THIRD-PARTY-NOTICES.md before releasing." }
& git -C $projectDir diff --quiet -- THIRD-PARTY-NOTICES.md
if ($LASTEXITCODE -ne 0) {
    & git -C $projectDir commit -q -m 'chore: record the cameraunlock-core commit this build compiles' -- THIRD-PARTY-NOTICES.md
    if ($LASTEXITCODE -ne 0) { throw "Could not commit the re-synced THIRD-PARTY-NOTICES.md." }
    Write-Host 'THIRD-PARTY-NOTICES.md re-synced to the pinned cameraunlock-core commit.' -ForegroundColor Yellow
}

# Step 3: generate CHANGELOG from commits since the last tag. This is the gate
# that aborts when there are no user-facing commits, so run it BEFORE mutating
# any version files or building - a failure here then leaves a clean tree
# instead of stranding a half-applied version bump with no tag.
Write-Host "Generating CHANGELOG from commits..." -ForegroundColor Cyan
$changelogPath = Join-Path $projectDir "CHANGELOG.md"
# A repo with no tags yet still has a CHANGELOG, written by hand before the
# first release, and overwriting it here threw that away in the same unattended
# run that committed and pushed the result. With no tags there is no commit
# range, not no changelog: fall through to the normal path, which inserts.
$hasExistingTags = git tag -l 2>$null
$existingChangelog = if (Test-Path $changelogPath) { Get-Content $changelogPath -Raw } else { '' }
if (-not $hasExistingTags -and $existingChangelog -notmatch '(?m)^## \[') {
    $date = Get-Date -Format 'yyyy-MM-dd'
    $firstEntry = "# Changelog`n`n## [$Version] - $date`n`nFirst release.`n"
    Set-Content $changelogPath $firstEntry
    Write-Host "  First release - wrote initial CHANGELOG entry" -ForegroundColor Gray
} else {
    try {
        $changelogArgs = @{
            ChangelogPath = $changelogPath
            Version       = $Version
            ArtifactPaths = @(
                "src/",
                "cameraunlock-core/",
                "scripts/install.cmd",
                "scripts/uninstall.cmd"
            )
        }
        New-ChangelogFromCommits @changelogArgs
    } catch {
        if (-not $Force) {
            Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host "No user-facing changes to release. Re-run with -Force for a maintenance release." -ForegroundColor Yellow
            exit 1
        }
        Write-Host "No user-facing commits since last tag - writing maintenance entry (-Force)." -ForegroundColor Yellow
        Add-MaintenanceChangelogEntry -Path $changelogPath -NewVersion $Version
    }
}

# Step 4: update version in canonical sources.
# Every file holding a version string must be bumped here. If you add another, add it
# both below AND to the `git add` list at step 6 - the release.yml workflow validates
# CMakeLists.txt against the tag and will fail the run if any drift.
Write-Host "Updating version to $Version..." -ForegroundColor Cyan
(Get-Content $installCmd -Raw) -replace 'set "MOD_VERSION=.*?"', "set `"MOD_VERSION=$Version`"" | Set-Content $installCmd -NoNewline
(Get-Content $cmakePath -Raw) -replace 'project\(StarfieldHeadTracking VERSION \d+\.\d+\.\d+', "project(StarfieldHeadTracking VERSION $Version" | Set-Content $cmakePath -NoNewline
(Get-Content $pixiPath -Raw) -replace '(?m)^version = "\d+\.\d+\.\d+"', "version = `"$Version`"" | Set-Content $pixiPath -NoNewline
(Get-Content $constantsPath -Raw) -replace 'inline constexpr const char\* VERSION = "\d+\.\d+\.\d+";', "inline constexpr const char* VERSION = `"$Version`";" | Set-Content $constantsPath -NoNewline

# launcher-manifest.json is the canonical launcher manifest the launcher ingests; keep its version in lockstep.
$launcherManifest = Get-Content $manifestJsonPath -Raw | ConvertFrom-Json
$launcherManifest.mod_info.version = $Version
$launcherManifest | ConvertTo-Json -Depth 10 | Set-Content $manifestJsonPath -NoNewline

# Step 5: test, then package.
#
# `package`, not `build-release`: every gate that can reject a release lives in
# the packager - the manifest seed check, the core-commit check, the missing-doc
# and missing-vendor-file throws, and building the ZIPs themselves. Running only
# the compiler here meant those fired in CI, after the commit and the tag had
# already been pushed, and recovering meant deleting a published tag.
#
# The tests run first for the same reason: the camera boundary maths every
# rendered frame passes through has no other gate before a user's machine.
Write-Host "Running 'pixi run test'..." -ForegroundColor Cyan
Push-Location $projectDir
try {
    & pixi run test
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: tests failed (exit $LASTEXITCODE). Aborting release." -ForegroundColor Red
        exit 1
    }

    Write-Host "Running 'pixi run package'..." -ForegroundColor Cyan
    & pixi run package
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: package failed (exit $LASTEXITCODE). Aborting release." -ForegroundColor Red
        exit 1
    }
} finally { Pop-Location }

# Step 6: commit
Write-Host "Committing version change..." -ForegroundColor Cyan
git add $manifestJsonPath $changelogPath $installCmd $cmakePath $pixiPath $constantsPath
git commit -m "Release v$Version"
if ($LASTEXITCODE -ne 0) { throw "git commit failed" }

# Step 7: annotated tag
Write-Host "Creating annotated tag $tagName..." -ForegroundColor Cyan
git tag -a $tagName -m "Release v$Version"
if ($LASTEXITCODE -ne 0) { throw "git tag failed" }

# Step 8: push
Write-Host "Pushing to GitHub..." -ForegroundColor Cyan
git push origin main
if ($LASTEXITCODE -ne 0) { throw "git push of main failed" }
git push origin $tagName
if ($LASTEXITCODE -ne 0) { throw "git push of tag failed" }

Write-Host ""
Write-Host "Release $tagName initiated!" -ForegroundColor Green
Write-Host "GitHub Actions will build and publish installer + nexus ZIPs." -ForegroundColor Gray
