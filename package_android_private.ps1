# Moon Dancer - private Android bundle (ROM baked in).
#
# Builds the same debug-variant APK as the public route (native code pinned to
# -O3 by android/app/build.gradle) but first copies the user's mwalk.zip into
# android/app/src/main/assets/, which the app's first-run asset extraction
# unpacks to files/mwalk.zip - the path the launcher already looks for.  The
# copy is made only for the duration of the build and removed again in
# `finally`, so the checked-in tree (and every public build) stays ROM-free.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\package_android_private.ps1
#   powershell -ExecutionPolicy Bypass -File .\package_android_private.ps1 -RomZip D:\roms\mwalk.zip
#
# Output: dist\Moon-Dancer-private-<sha12>-android.apk
#
# THIS ARTIFACT CONTAINS GAME DATA.  NEVER PUBLISH IT.

[CmdletBinding()]
param(
    # ROM archive to bake in.  Defaults to .\mwalk.zip next to this script.
    [string]$RomZip = "",
    # Reuse an existing APK if the Gradle build is not wanted (testing only).
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$assetDir = Join-Path $root "android\app\src\main\assets"
$asset = Join-Path $assetDir "mwalk.zip"
$project = Join-Path $root "android"
$gradlew = Join-Path $project "gradlew.bat"
$apkIn = Join-Path $project "app\build\outputs\apk\debug\app-debug.apk"
$dist = Join-Path $root "dist"

$rom = if ($RomZip) { $RomZip } else { Join-Path $root "mwalk.zip" }
if (-not (Test-Path -LiteralPath $rom)) {
    throw "ROM archive not found: $rom  (pass -RomZip <path> or put mwalk.zip next to this script)"
}

# Gradle needs to know where the SDK is.  Honor the environment when set,
# otherwise fall back to the standard per-user install; local.properties is
# gitignored, so writing it here never pollutes the repository.  Only resolved
# when the file is missing: an existing local.properties is authoritative.
$localProps = Join-Path $project "local.properties"
if (-not (Test-Path -LiteralPath $localProps)) {
    $sdk = $env:ANDROID_HOME
    if (-not $sdk) { $sdk = $env:ANDROID_SDK_ROOT }
    if (-not $sdk) {
        if (-not $env:LOCALAPPDATA) {
            throw "Android SDK location unknown: set ANDROID_HOME (or ANDROID_SDK_ROOT) to the SDK folder, or create android/local.properties with sdk.dir=<path>, then re-run."
        }
        $sdk = Join-Path $env:LOCALAPPDATA "Android\Sdk"
    }
    if (Test-Path -LiteralPath $sdk) {
        $escaped = $sdk -replace '\\', '\\'
        Set-Content -LiteralPath $localProps -Value "sdk.dir=$escaped" -Encoding ASCII
        Write-Host "Wrote $localProps"
    }
    else {
        Write-Warning "Android SDK not found at $sdk; leaving android/local.properties absent. Set ANDROID_HOME or create local.properties before building."
    }
}

$sha = (Get-FileHash -LiteralPath $rom -Algorithm SHA256).Hash.Substring(0, 12).ToLowerInvariant()
$outName = "Moon-Dancer-private-$sha-android.apk"
$outPath = Join-Path $dist $outName

$baked = $false
$backedUp = $false
$backup = "$asset.pre-bundle"
try {
    if (-not (Test-Path -LiteralPath $assetDir)) {
        New-Item -ItemType Directory -Path $assetDir -Force | Out-Null
    }
    # A file staged by hand in the assets tree is the user's: park it and put
    # it back in `finally` rather than overwriting or deleting it.  A leftover
    # backup means an earlier run was interrupted; refuse rather than guess.
    if (Test-Path -LiteralPath $backup) {
        throw "A leftover assets/mwalk.zip.pre-bundle exists (an earlier run may have been interrupted). Inspect and move it away, then re-run."
    }
    if (Test-Path -LiteralPath $asset) {
        Move-Item -LiteralPath $asset -Destination $backup
        $backedUp = $true
        Write-Host "Backed up the existing assets/mwalk.zip to $(Split-Path -Leaf $backup)"
    }
    # Set before the copy so a failed or partial Copy-Item is still cleaned up.
    $baked = $true
    Copy-Item -LiteralPath $rom -Destination $asset -Force
    Write-Host ("Bundled {0} as assets/mwalk.zip ({1:N0} bytes)" -f (Split-Path -Leaf $rom), (Get-Item -LiteralPath $asset).Length)

    if (-not $SkipBuild) {
        if (-not (Test-Path -LiteralPath $gradlew)) {
            throw "Gradle wrapper not found: $gradlew"
        }
        Push-Location $project
        try {
            # --no-daemon: a detached Gradle daemon inherits this script's
            # process tree, so a caller that waits for the whole tree (CI,
            # Start-Process -Wait) would appear to hang long after the APK is
            # written.  A packaging run is infrequent enough that the cold
            # start does not matter.  --console=plain keeps the log readable
            # when stdout is redirected to a file.
            & $gradlew :app:assembleDebug --no-daemon --console=plain
            if ($LASTEXITCODE -ne 0) {
                throw "Gradle assembleDebug failed with exit code $LASTEXITCODE"
            }
        }
        finally {
            Pop-Location
        }
    }
    if (-not (Test-Path -LiteralPath $apkIn)) {
        throw "APK not found: $apkIn"
    }

    # Prove the ROM really is inside the artifact: this build's whole point is
    # the baked archive, so a silently missing asset must fail loudly.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($apkIn)
    try {
        $entry = $zip.Entries | Where-Object { $_.FullName -eq "assets/mwalk.zip" } | Select-Object -First 1
        if (-not $entry) {
            throw "assets/mwalk.zip is missing from the built APK"
        }
        $romBytes = $entry.Length
    }
    finally {
        $zip.Dispose()
    }

    New-Item -ItemType Directory -Path $dist -Force | Out-Null
    Copy-Item -LiteralPath $apkIn -Destination $outPath -Force
    # The dist artifact is the only copy that should keep the ROM.  Remove the
    # build-tree APK and the merged asset copy Gradle made from it, so a later
    # `assembleDebug` cannot quietly package yesterday's game data.
    Remove-Item -LiteralPath $apkIn -Force
    $mergedAsset = Join-Path $project "app\build\intermediates\assets\debug\mwalk.zip"
    if (Test-Path -LiteralPath $mergedAsset) {
        Remove-Item -LiteralPath $mergedAsset -Force
    }
    Write-Host "Removed the build-tree APK ($outName is the only ROM-bearing copy)"
    Write-Host ""
    Write-Host "Built $outPath"
    Write-Host ("  ROM inside: assets/mwalk.zip ({0:N0} bytes), sha256 {1}..." -f $romBytes, $sha)
}
finally {
    # Never leave game data in the assets tree: a public build must be
    # ROM-free, and this directory is what it would silently pick up.
    if ($baked -and (Test-Path -LiteralPath $asset)) {
        Remove-Item -LiteralPath $asset -Force
        Write-Host "Removed the temporary assets/mwalk.zip"
    }
    if ($backedUp -and (Test-Path -LiteralPath $backup)) {
        Move-Item -LiteralPath $backup -Destination $asset
        Write-Host "Restored the pre-existing assets/mwalk.zip"
    }
}

Write-Host ""
Write-Host "=================================================================="
Write-Host "  NEVER PUBLISH THIS BUILD."
Write-Host "  This APK contains Moonwalker game data (your mwalk.zip)."
Write-Host "  Keep it for your own device only; redistribute nothing."
Write-Host "=================================================================="
