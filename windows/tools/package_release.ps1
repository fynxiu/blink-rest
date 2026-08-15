param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string]$Version,

    [string]$SourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [string]$BuildDir,
    [string]$DistDir
)

$ErrorActionPreference = 'Stop'

if (-not $BuildDir) {
    $BuildDir = Join-Path $SourceDir 'build-release'
}
if (-not $DistDir) {
    $DistDir = Join-Path $SourceDir ('dist\v' + $Version)
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE"
    }
}

function Find-WindowsSdkTool {
    param([Parameter(Mandatory = $true)][string]$Name)

    $fromPath = Get-Command $Name -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    $programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
    $kitsRoot = Join-Path $programFilesX86 'Windows Kits\10\bin'
    if (-not (Test-Path $kitsRoot)) {
        return $null
    }

    $candidate = Get-ChildItem $kitsRoot -Recurse -Filter $Name -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (-not $candidate) {
        $candidate = Get-ChildItem $kitsRoot -Recurse -Filter $Name -File -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            Select-Object -First 1
    }
    if ($candidate) {
        return $candidate.FullName
    }
    return $null
}

function Find-InnoSetup {
    $userCandidate = Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'
    if (Test-Path $userCandidate) {
        return $userCandidate
    }

    $roots = @(
        [Environment]::GetFolderPath('ProgramFilesX86'),
        [Environment]::GetFolderPath('ProgramFiles')
    )
    foreach ($root in $roots) {
        if (-not $root) {
            continue
        }
        $candidate = Join-Path $root 'Inno Setup 6\ISCC.exe'
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    $fromPath = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }
    return $null
}

$tag = 'v' + $Version
$assetName = "BlinkRest-$tag-windows-x64.zip"
$assetPath = Join-Path $DistDir $assetName
$checksumsPath = Join-Path $DistDir 'SHA256SUMS.txt'
$stagingDir = Join-Path $BuildDir 'release-package'

Write-Output "Packaging Blink Rest $Version for Windows x64"
Write-Output "Source: $SourceDir"
Write-Output "Build:  $BuildDir"
Write-Output "Dist:   $DistDir"

New-Item -ItemType Directory -Force $BuildDir | Out-Null
New-Item -ItemType Directory -Force $DistDir | Out-Null

Invoke-Checked 'cmake' @(
    '-S', $SourceDir,
    '-B', $BuildDir,
    '-G', 'Visual Studio 17 2022',
    '-A', 'x64',
    ("-DBLINKREST_VERSION=" + $Version)
)
Invoke-Checked 'cmake' @('--build', $BuildDir, '--config', 'Release')
Invoke-Checked 'ctest' @('--test-dir', $BuildDir, '-C', 'Release', '--output-on-failure')

$executable = Join-Path $BuildDir 'Release\blinkrest_win32.exe'
if (-not (Test-Path $executable)) {
    throw "release executable not found: $executable"
}

$versionInfo = (Get-Item $executable).VersionInfo
if ($versionInfo.ProductVersion -ne $Version) {
    throw "release product version is $($versionInfo.ProductVersion), expected $Version"
}
Write-Output ("PRODUCT_VERSION=" + $versionInfo.ProductVersion)

$smoke = Start-Process -FilePath $executable -ArgumentList '--headless-smoke' -Wait -PassThru
if ($smoke.ExitCode -ne 0) {
    throw "headless smoke failed with exit code $($smoke.ExitCode)"
}

if (Test-Path $stagingDir) {
    Remove-Item $stagingDir -Recurse -Force
}
New-Item -ItemType Directory -Force $stagingDir | Out-Null
Copy-Item $executable (Join-Path $stagingDir 'BlinkRest.exe')
Copy-Item (Join-Path $SourceDir 'README.md') (Join-Path $stagingDir 'README.md')

$signTool = Find-WindowsSdkTool 'signtool.exe'
$signature = Get-AuthenticodeSignature $executable
if ($signTool) {
    Write-Output ("SIGNTOOL=" + $signTool)
} else {
    Write-Output 'SIGNTOOL=MISSING'
}
Write-Output ("SIGNATURE_STATUS=" + $signature.Status)
if ($signature.Status -ne 'Valid') {
    Write-Warning 'Executable is not Authenticode-signed. Packaging continues for local/test distribution only.'
}

if (Test-Path $assetPath) {
    Remove-Item $assetPath -Force
}
Compress-Archive -Path (Join-Path $stagingDir '*') -DestinationPath $assetPath -CompressionLevel Optimal

$innoSetup = Find-InnoSetup
if (-not $innoSetup) {
    throw 'Inno Setup 6 compiler (ISCC.exe) is required to create the Windows installer.'
}
Write-Output ('INNO_SETUP=' + $innoSetup)
$installerScript = Join-Path $SourceDir 'installer\BlinkRest.iss'
$readmePath = Join-Path $SourceDir 'README.md'
Invoke-Checked $innoSetup @(
    ('/DMyAppVersion=' + $Version),
    ('/DSourceExe=' + $executable),
    ('/DSourceReadme=' + $readmePath),
    ('/DOutputDir=' + $DistDir),
    $installerScript
)
$installerPath = Join-Path $DistDir ('BlinkRest-v' + $Version + '-windows-x64-setup.exe')
if (-not (Test-Path $installerPath)) {
    throw "expected installer not found: $installerPath"
}
$installerSignature = Get-AuthenticodeSignature $installerPath
Write-Output ('INSTALLER_SIGNATURE_STATUS=' + $installerSignature.Status)
if ($installerSignature.Status -ne 'Valid') {
    Write-Warning 'Installer is not Authenticode-signed. Packaging continues for local/test distribution only.'
}

$checksumLines = foreach ($artifactPath in @($assetPath, $installerPath)) {
    $hash = (Get-FileHash -Algorithm SHA256 $artifactPath).Hash.ToLowerInvariant()
    $hash + '  ' + (Split-Path -Leaf $artifactPath)
}
Set-Content -Path $checksumsPath -Encoding ASCII -Value $checksumLines

$exeSize = (Get-Item $executable).Length
$zipSize = (Get-Item $assetPath).Length
$installerSize = (Get-Item $installerPath).Length
Write-Output 'HEADLESS_SMOKE=PASS'
Write-Output "EXE_BYTES=$exeSize"
Write-Output "ZIP_BYTES=$zipSize"
Write-Output "ASSET=$assetPath"
Write-Output "INSTALLER_BYTES=$installerSize"
Write-Output "ASSET=$installerPath"
Write-Output "CHECKSUMS=$checksumsPath"
