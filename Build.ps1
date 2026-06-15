[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',

    [string]$Runtime = 'win-x64',

    [switch]$SelfContained,

    [switch]$FrameworkDependent,

    [switch]$NoClean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectRoot = $PSScriptRoot
$BackendSource = Join-Path $ProjectRoot 'backend'
$BackendBuild = Join-Path $ProjectRoot 'build\backend'
$FrontendProject = Join-Path $ProjectRoot 'frontend\Fluxora.App.csproj'
$OutputDir = Join-Path $ProjectRoot 'Output'

function Assert-Command {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Command '$Name' was not found. Install it and make sure it is available in PATH."
    }
}

function Invoke-BuildStep {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Title,

        [Parameter(Mandatory = $true)]
        [scriptblock]$Action
    )

    Write-Host ""
    Write-Host "==> $Title"
    & $Action
}

function Get-NativeCorePath {
    $knownPaths = @(
        (Join-Path $BackendBuild "$Configuration\FluxoraCore.dll"),
        (Join-Path $BackendBuild 'FluxoraCore.dll')
    )

    foreach ($path in $knownPaths) {
        if (Test-Path -LiteralPath $path) {
            return $path
        }
    }

    $latestDll = Get-ChildItem -LiteralPath $BackendBuild -Recurse -Filter 'FluxoraCore.dll' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    if ($latestDll) {
        return $latestDll.FullName
    }

    throw "FluxoraCore.dll was not found under '$BackendBuild'."
}

function Get-NativeVfsPath {
    $knownPaths = @(
        (Join-Path $BackendBuild "$Configuration\FluxoraVfs.dll"),
        (Join-Path $BackendBuild 'FluxoraVfs.dll')
    )

    foreach ($path in $knownPaths) {
        if (Test-Path -LiteralPath $path) {
            return $path
        }
    }

    $latestDll = Get-ChildItem -LiteralPath $BackendBuild -Recurse -Filter 'FluxoraVfs.dll' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    if ($latestDll) {
        return $latestDll.FullName
    }

    return $null
}

Assert-Command 'cmake'
Assert-Command 'dotnet'

if ($SelfContained -and $FrameworkDependent) {
    throw "Use either -SelfContained or -FrameworkDependent, not both."
}

$publishSelfContained = -not $FrameworkDependent
if ($publishSelfContained -and [string]::IsNullOrWhiteSpace($Runtime)) {
    throw "Self-contained publish requires a runtime. Example: -Runtime win-x64"
}

if (-not (Test-Path -LiteralPath (Join-Path $BackendSource 'CMakeLists.txt'))) {
    throw "Backend CMake project was not found at '$BackendSource'."
}

if (-not (Test-Path -LiteralPath $FrontendProject)) {
    throw "Frontend project was not found at '$FrontendProject'."
}

$projectRootFullPath = [System.IO.Path]::GetFullPath($ProjectRoot)
$outputFullPath = [System.IO.Path]::GetFullPath($OutputDir)

if (-not $outputFullPath.StartsWith($projectRootFullPath, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Output directory '$outputFullPath' is outside the project root '$projectRootFullPath'."
}

Invoke-BuildStep "Preparing Output folder" {
    if ((Test-Path -LiteralPath $OutputDir) -and (-not $NoClean)) {
        Remove-Item -LiteralPath $OutputDir -Recurse -Force
    }

    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $OutputDir 'logs') -Force | Out-Null
}

Invoke-BuildStep "Configuring C++ backend" {
    & cmake -S $BackendSource -B $BackendBuild

    $cmakeCachePath = Join-Path $BackendBuild 'CMakeCache.txt'
    $cmakeCache = Get-Content -LiteralPath $cmakeCachePath -Raw
    $isMultiConfigGenerator = $cmakeCache -match '(?m)^CMAKE_CONFIGURATION_TYPES(:[A-Z]+)?='

    if (-not $isMultiConfigGenerator) {
        & cmake -S $BackendSource -B $BackendBuild -DCMAKE_BUILD_TYPE=$Configuration
    }
}

Invoke-BuildStep "Building C++ backend ($Configuration)" {
    # Build every target (FluxoraCore, the FluxoraVfs hook DLL and Detours) so the
    # virtual file system ships alongside the core.
    & cmake --build $BackendBuild --config $Configuration
}

Invoke-BuildStep "Publishing C# frontend ($Configuration)" {
    $publishArgs = @(
        'publish',
        $FrontendProject,
        '--configuration',
        $Configuration,
        '--output',
        $OutputDir,
        '--self-contained',
        $(if ($publishSelfContained) { 'true' } else { 'false' })
    )

    if (-not [string]::IsNullOrWhiteSpace($Runtime)) {
        $publishArgs += @('--runtime', $Runtime)
    }

    & dotnet @publishArgs
}

Invoke-BuildStep "Copying native backend to Output" {
    $nativeCorePath = Get-NativeCorePath
    Copy-Item -LiteralPath $nativeCorePath -Destination $OutputDir -Force

    $nativePdbPath = [System.IO.Path]::ChangeExtension($nativeCorePath, '.pdb')
    if (Test-Path -LiteralPath $nativePdbPath) {
        Copy-Item -LiteralPath $nativePdbPath -Destination $OutputDir -Force
    }

    # The injected virtual file system hook must sit next to FluxoraCore.dll so the
    # core can locate and inject it when launching a game.
    $nativeVfsPath = Get-NativeVfsPath
    if ($nativeVfsPath) {
        Copy-Item -LiteralPath $nativeVfsPath -Destination $OutputDir -Force

        $nativeVfsPdbPath = [System.IO.Path]::ChangeExtension($nativeVfsPath, '.pdb')
        if (Test-Path -LiteralPath $nativeVfsPdbPath) {
            Copy-Item -LiteralPath $nativeVfsPdbPath -Destination $OutputDir -Force
        }
    }
    else {
        Write-Warning "FluxoraVfs.dll was not found; the build will run without the virtual file system."
    }
}

$appExePath = Join-Path $OutputDir 'Fluxora.App.exe'
if (-not (Test-Path -LiteralPath $appExePath)) {
    throw "Build completed, but Fluxora.App.exe was not found in '$OutputDir'."
}

Write-Host ""
Write-Host "Done. Project output is ready:"
Write-Host "  $OutputDir"
