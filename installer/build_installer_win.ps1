# Build the Tube Channel Windows installer from a Release build.
#   powershell -ExecutionPolicy Bypass -File installer\build_installer_win.ps1
# Stages the user guide and third-party license texts, then runs Inno Setup.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)

$Version = (Get-Content VERSION -Raw).Trim()
$Art = "build\TubeChannel_artefacts\Release"
$Out = "dist"
$Stage = "$Out\stage"

foreach ($required in @(
    "$Art\VST3\Tube Channel.vst3",
    "$Art\CLAP\Tube Channel.clap",
    "$Art\LV2\Tube Channel.lv2",
    "$Art\Standalone\Tube Channel.exe")) {
    if (-not (Test-Path $required)) {
        throw "Missing build artifact: $required (run the Release build first)"
    }
}

function CacheValue($name) {
    $line = Select-String -Path "build\CMakeCache.txt" -Pattern "^$name`:[^=]*=" | Select-Object -First 1
    if ($line) { return ($line.Line -split '=', 2)[1] }
    return $null
}
$JuceSrc = CacheValue "FETCHCONTENT_SOURCE_DIR_JUCE"
if (-not $JuceSrc) { $JuceSrc = "build\_deps\juce-src" }
$ClapExtSrc = CacheValue "FETCHCONTENT_SOURCE_DIR_CLAP-JUCE-EXTENSIONS"
if (-not $ClapExtSrc) { $ClapExtSrc = "build\_deps\clap-juce-extensions-src" }

if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
$ResourceRoot = "$Stage\licenses\Tube Channel"
$LicenseRoot = "$ResourceRoot\Licenses"
foreach ($d in @("$LicenseRoot\Tube Channel", "$LicenseRoot\GoodLookinUI", "$LicenseRoot\JUCE",
                 "$LicenseRoot\CLAP", "$LicenseRoot\VST3-SDK", "$LicenseRoot\LV2-SDK",
                 "$ResourceRoot\Manual")) {
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}

Copy-Item "docs\USER_MANUAL.md", "docs\ui-analogue.png", "docs\ui-digital.png", "docs\ui-system-analogue.png", "docs\ui-system-digital.png", "docs\signal-path.svg" -Destination "$ResourceRoot\Manual\"
Copy-Item "output\pdf\TubeChannel-User-Manual.pdf" -Destination "$ResourceRoot\Manual\"
foreach ($image in @("docs\offline-bounce-analogue.png", "docs\offline-bounce-digital.png")) {
    if (Test-Path $image) { Copy-Item $image "$ResourceRoot\Manual\" }
}
if (Test-Path "docs\RELEASE_$Version.md") { Copy-Item "docs\RELEASE_$Version.md" "$ResourceRoot\Manual\" }
Copy-Item "LICENSE", "THIRD_PARTY_LICENSES.md" $ResourceRoot
Copy-Item "LICENSE", "THIRD_PARTY_LICENSES.md" "$LicenseRoot\Tube Channel\"
Copy-Item "third_party\GoodLookinUI\LICENSE" "$LicenseRoot\GoodLookinUI\"
Copy-Item "$JuceSrc\LICENSE.md" "$LicenseRoot\JUCE\LICENSE.md"
Copy-Item "$ClapExtSrc\LICENSE.md" "$LicenseRoot\CLAP\clap-juce-extensions-LICENSE.md"
Copy-Item "$ClapExtSrc\clap-libs\clap\LICENSE" "$LicenseRoot\CLAP\clap-LICENSE"
Copy-Item "$ClapExtSrc\clap-libs\clap-helpers\LICENSE" "$LicenseRoot\CLAP\clap-helpers-LICENSE"

# Notices for code shipped in the Windows binaries: codecs, graphics helpers, and the
# VST3 and LV2 SDKs. Audio Unit is macOS-only and is not part of this package.
$SearchRoots = @(
    "$JuceSrc\modules\juce_audio_formats\codecs",
    "$JuceSrc\modules\juce_core\zip\zlib",
    "$JuceSrc\modules\juce_graphics\drawables\lunasvg",
    "$JuceSrc\modules\juce_graphics\fonts\harfbuzz",
    "$JuceSrc\modules\juce_graphics\image_formats\libwebp",
    "$JuceSrc\modules\juce_graphics\image_formats\pnglib",
    "$JuceSrc\modules\juce_graphics\unicode\sheenbidi",
    "$JuceSrc\modules\juce_audio_processors_headless\format_types\VST3_SDK",
    "$JuceSrc\modules\juce_audio_processors_headless\format_types\LV2_SDK"
)
Get-ChildItem -Path $SearchRoots -Recurse -File -Include "LICENSE*", "COPYING*", "NOTICE*" |
    ForEach-Object {
        $p = $_.FullName
        if ($p -match [regex]::Escape("format_types\VST3_SDK\") ) {
            $rel = $p -replace ".*format_types\\VST3_SDK\\", ""
            $dest = "$LicenseRoot\VST3-SDK\$rel"
        } elseif ($p -match [regex]::Escape("format_types\LV2_SDK\")) {
            $rel = $p -replace ".*format_types\\LV2_SDK\\", ""
            $dest = "$LicenseRoot\LV2-SDK\$rel"
        } elseif ($p -match [regex]::Escape("\modules\")) {
            $rel = $p -replace ".*\\modules\\", ""
            $dest = "$LicenseRoot\JUCE\modules\$rel"
        } else { return }
        New-Item -ItemType Directory -Force -Path (Split-Path $dest -Parent) | Out-Null
        Copy-Item $p $dest
    }

Get-ChildItem -Recurse -File $LicenseRoot |
    ForEach-Object { $_.FullName.Substring((Resolve-Path $LicenseRoot).Path.Length + 1).Replace('\', '/') } |
    Sort-Object | Set-Content "$LicenseRoot\INDEX.txt"

@"
TUBE CHANNEL $Version - BINARY LICENSE NOTICE

The Tube Channel binaries (VST3, CLAP, LV2 and standalone application) are
distributed as freeware. You may install and use them without charge.
Copyright (c) Marcos Deida / OpenGrid. All rights reserved.

The Tube Channel source code is MIT licensed; see LICENSE in this package.
The applicable third-party license texts are under Licenses\ (see INDEX.txt).
JUCE is used under a commercial JUCE Developer license held by the author.

$(Get-Content LICENSE -Raw)
"@ | Set-Content "$ResourceRoot\LICENSE_BINARY.txt"

$Iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
          "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
          "C:\Program Files\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Iscc) { throw "Inno Setup 6 not found (install JRSoftware.InnoSetup via winget)" }
& $Iscc "/DMyAppVersion=$Version" "installer\TubeChannel.iss"
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }
Write-Host "built $Out\TubeChannel-$Version-win.exe"
