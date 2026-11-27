[CmdletBinding()]
param(
    [string]$WebRtcRoot,
    [string]$NvCodecRoot,
    [string]$BuildDirectory,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
function Invoke-CMake([string[]]$Arguments) {
    & cmake @Arguments
    if ($LASTEXITCODE -ne 0) { throw "CMake failed with exit code $LASTEXITCODE" }
}

$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$source = $PSScriptRoot
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $workspace 'build/windows-rtc-sdk'
    $consoleBuild = Join-Path $workspace 'build/windows-console'
    if (-not $WebRtcRoot -and -not $NvCodecRoot -and
        -not (Test-Path -LiteralPath "$BuildDirectory/CMakeCache.txt") -and
        (Test-Path -LiteralPath "$consoleBuild/CMakeCache.txt")) {
        $source = Join-Path $PSScriptRoot '../dozer_rtc_console'
        $BuildDirectory = $consoleBuild
    }
}
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $workspace 'install/rtc-sdk' }
$BuildDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDirectory)
$OutputDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
$configure = @('-S', $source, '-B', $BuildDirectory, '-G', 'Visual Studio 17 2022', '-A', 'x64')
if ($WebRtcRoot) { $configure += "-DDOZER_WEBRTC_ROOT=$WebRtcRoot" }
if ($NvCodecRoot) { $configure += "-DDOZER_NVCODEC_ROOT=$NvCodecRoot" }

Invoke-CMake $configure
Invoke-CMake @('--build', $BuildDirectory, '--config', 'Release', '--target', 'vtsrtc', '--parallel')
$package = Join-Path $OutputDirectory 'VtsRtc'
Invoke-CMake @('--install', $BuildDirectory, '--config', 'Release', '--component', 'RtcSdk', '--prefix', $package)

$archive = Join-Path $OutputDirectory 'VtsRtc-Win64.zip'
Push-Location -LiteralPath $OutputDirectory
try {
    $files = @(Get-Content -LiteralPath "$BuildDirectory/install_manifest_RtcSdk.txt" -Encoding UTF8 | ForEach-Object {
        (Resolve-Path -Relative -LiteralPath $_).Replace('\', '/')
    })
    if (-not $files.Count) { throw 'Empty RTC SDK install manifest' }
    Invoke-CMake (@('-E', 'tar', 'cf', $archive, '--format=zip', '--') + $files)
} finally {
    Pop-Location
}
Write-Host "UE4 module: $package"
Write-Host "ZIP: $archive"
