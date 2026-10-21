# Package the Windows x64 GrooveMorph beta build into GrooveMorph-<version>-windows-x64.zip.
#
# Usage (PowerShell 7):
#   ./tools/package/package.ps1 -Version 0.2.0-beta.1 -BuildDir build -OutDir dist `
#       [-Commit SHA] [-Runner NAME] [-RunUrl URL] [-CtestLog FILE] [-ValidatorDir DIR]
#
# Fails when any expected artefact is missing. Writes into OutDir the same three files as package.sh:
# the .zip, the .zip.sha256 and the .status.txt.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$BuildDir,
    [Parameter(Mandatory)][string]$OutDir,
    [string]$Commit = 'unknown',
    [string]$Runner = 'unknown',
    [string]$RunUrl = 'n/a',
    [string]$CtestLog = '',
    [string]$ValidatorDir = ''
)
$ErrorActionPreference = 'Stop'
$platform = 'windows-x64'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$artefacts = Join-Path $BuildDir 'GrooveMorphPlugin_artefacts'
if (-not (Test-Path $artefacts)) { throw "no artefacts directory: $artefacts" }

function Find-One([string]$Kind, [string]$Item) {
    $hit = Get-ChildItem -Path $artefacts -Recurse -Depth 2 -Force |
        Where-Object { $_.Name -eq $Item -and (Split-Path (Split-Path $_.FullName -Parent) -Leaf) -eq $Kind } |
        Select-Object -First 1
    if (-not $hit) { throw "missing artefact: $Kind/$Item under $artefacts" }
    return $hit.FullName
}
function Sha256([string]$Path) { (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToLower() }

$standalone = Find-One 'Standalone' 'GrooveMorph.exe'
$vst3 = Find-One 'VST3' 'GrooveMorph.vst3'
$clap = Find-One 'CLAP' 'GrooveMorph.clap'

$name = "GrooveMorph-$Version-$platform"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stage = Join-Path $OutDir $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Path $stage | Out-Null

Copy-Item $standalone (Join-Path $stage 'GrooveMorph.exe')
Copy-Item $vst3 (Join-Path $stage 'GrooveMorph.vst3') -Recurse
Copy-Item $clap (Join-Path $stage 'GrooveMorph.clap')

Copy-Item (Join-Path $repoRoot 'docs/beta/INSTALL.md') (Join-Path $stage 'INSTALL.md')
Copy-Item (Join-Path $repoRoot 'THIRD-PARTY-NOTICES.md') (Join-Path $stage 'THIRD-PARTY-NOTICES.md')
Copy-Item (Join-Path $repoRoot 'docs/alpha/KNOWN-ISSUES.md') (Join-Path $stage 'KNOWN-ISSUES.md')
Copy-Item (Join-Path $repoRoot 'VERSION') (Join-Path $stage 'VERSION')
$quick = Join-Path $repoRoot 'help/QUICK-START.md'
if (Test-Path $quick) { Copy-Item $quick (Join-Path $stage 'QUICK-START.md') }

$ctestStatus = 'passed (see ctest.log)'
if ($CtestLog -and (Test-Path $CtestLog)) { Copy-Item $CtestLog (Join-Path $stage 'ctest.log') } else { $ctestStatus = 'log not provided' }

$validatorStatus = 'not run'
if ($ValidatorDir -and (Test-Path (Join-Path $ValidatorDir 'clap-validator-status.txt'))) {
    $validatorStatus = (Get-Content (Join-Path $ValidatorDir 'clap-validator-status.txt') -Raw).Trim()
    $vlog = Join-Path $ValidatorDir 'clap-validator.log'
    if (Test-Path $vlog) { Copy-Item $vlog (Join-Path $stage 'clap-validator.log') }
}

$vst3Bin = Get-ChildItem -Path (Join-Path $stage 'GrooveMorph.vst3') -Recurse -Filter '*.vst3' -File | Select-Object -First 1
if (-not $vst3Bin) { throw 'could not locate the VST3 binary inside the bundle' }

@"
# Build manifest

- Version: $Version
- Platform: $platform
- Commit: $Commit
- Workflow run: $RunUrl
- Runner: $Runner
- Standalone binary SHA-256: $(Sha256 (Join-Path $stage 'GrooveMorph.exe'))
- VST3 binary SHA-256: $(Sha256 $vst3Bin.FullName)
- CLAP binary SHA-256: $(Sha256 (Join-Path $stage 'GrooveMorph.clap'))
- ctest: $ctestStatus
- clap-validator: $validatorStatus
- Signing: none (unsigned)
- NOT performed: DAW hosting of any format, audio-device playback, listening tests, installer, clean-machine install test.
"@ | Set-Content -Path (Join-Path $stage 'BUILD-MANIFEST.md') -Encoding utf8

$zipPath = Join-Path $OutDir "$name.zip"
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
Compress-Archive -Path $stage -DestinationPath $zipPath
$zipSha = Sha256 $zipPath
"$zipSha  $name.zip" | Set-Content -Path "$zipPath.sha256" -Encoding ascii

@"
platform=$platform
runner=$Runner
ctest=$ctestStatus
clap_validator=$validatorStatus
signing=none (unsigned)
zip=$name.zip
sha256=$zipSha
"@ | Set-Content -Path (Join-Path $OutDir "$name.status.txt") -Encoding ascii
Write-Host "packaged $zipPath (sha256 $zipSha)"
