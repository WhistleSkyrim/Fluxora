[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',

    [string]$Runtime = 'win-x64',

    [switch]$SelfContained,

    [switch]$FrameworkDependent,

    [switch]$LooseFiles,

    [switch]$NoClean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectRoot = $PSScriptRoot
$BackendSource = Join-Path $ProjectRoot 'backend'
$BackendBuild = Join-Path $ProjectRoot 'build\backend'
$FrontendProject = Join-Path $ProjectRoot 'frontend\Fluxora.App.csproj'
$FrontendExecutableName = 'FluxoraModding.exe'
$InstallerProject = Join-Path $ProjectRoot 'installer\Fluxora.Installer\Fluxora.Installer.csproj'
$OutputDir = Join-Path $ProjectRoot 'output'
$InstallerOutputDir = Join-Path $ProjectRoot 'output-installer'
$InstallerPayloadDir = Join-Path $ProjectRoot 'installer\Fluxora.Installer\Resources\Payload'
$InstallerPayloadPath = Join-Path $InstallerPayloadDir 'FluxoraPayload.flxpkg'

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

function Get-NativeInstallerCorePath {
    $knownPaths = @(
        (Join-Path $BackendBuild "$Configuration\FluxoraInstallerCore.dll"),
        (Join-Path $BackendBuild 'FluxoraInstallerCore.dll')
    )

    foreach ($path in $knownPaths) {
        if (Test-Path -LiteralPath $path) {
            return $path
        }
    }

    $latestDll = Get-ChildItem -LiteralPath $BackendBuild -Recurse -Filter 'FluxoraInstallerCore.dll' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    if ($latestDll) {
        return $latestDll.FullName
    }

    throw "FluxoraInstallerCore.dll was not found under '$BackendBuild'."
}

function Assert-ChildPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$ParentPath
    )

    $parentFullPath = [System.IO.Path]::GetFullPath($ParentPath)
    $childFullPath = [System.IO.Path]::GetFullPath($Path)

    if (-not $childFullPath.StartsWith($parentFullPath, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path '$childFullPath' is outside the project root '$parentFullPath'."
    }
}

function Get-PortableRelativePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,

        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $rootFullPath = [System.IO.Path]::GetFullPath($Root)
    $pathFullPath = [System.IO.Path]::GetFullPath($Path)
    $trimChars = @([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)

    if ([string]::Equals($rootFullPath.TrimEnd($trimChars), $pathFullPath.TrimEnd($trimChars), [System.StringComparison]::OrdinalIgnoreCase)) {
        return '.'
    }

    if (-not $rootFullPath.EndsWith([System.IO.Path]::DirectorySeparatorChar) -and
        -not $rootFullPath.EndsWith([System.IO.Path]::AltDirectorySeparatorChar)) {
        $rootFullPath += [System.IO.Path]::DirectorySeparatorChar
    }

    $rootUri = [System.Uri]::new($rootFullPath)
    $pathUri = [System.Uri]::new($pathFullPath)
    $relative = [System.Uri]::UnescapeDataString($rootUri.MakeRelativeUri($pathUri).ToString())

    if ([string]::IsNullOrEmpty($relative)) {
        return '.'
    }

    return $relative.Replace('\', '/')
}

function Write-FluxoraPayloadPackage {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourceDirectory,

        [Parameter(Mandatory = $true)]
        [string]$PackagePath
    )

    if (-not (Test-Path -LiteralPath $SourceDirectory)) {
        throw "Portable output directory '$SourceDirectory' does not exist."
    }

    $sourceFullPath = [System.IO.Path]::GetFullPath($SourceDirectory)
    $directories = Get-ChildItem -LiteralPath $sourceFullPath -Directory -Recurse -Force |
        Sort-Object FullName
    $files = Get-ChildItem -LiteralPath $sourceFullPath -File -Recurse -Force |
        Where-Object {
            $relative = Get-PortableRelativePath -Root $sourceFullPath -Path $_.FullName
            -not ($relative -like 'logs/*.log')
        } |
        Sort-Object FullName

    $entryCount = [UInt64]($directories.Count + $files.Count)
    $totalBytes = [UInt64]0
    foreach ($file in $files) {
        $totalBytes += [UInt64]$file.Length
    }

    New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($PackagePath)) -Force | Out-Null

    $stream = [System.IO.File]::Open($PackagePath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
    try {
        $writer = [System.IO.BinaryWriter]::new($stream, [System.Text.Encoding]::UTF8, $true)
        try {
            $writer.Write([byte[]](0x46, 0x4C, 0x58, 0x50, 0x4B, 0x47, 0x31, 0x00))
            $writer.Write([UInt32]1)
            $writer.Write($entryCount)
            $writer.Write($totalBytes)

            foreach ($directory in $directories) {
                $relative = Get-PortableRelativePath -Root $sourceFullPath -Path $directory.FullName
                if ([string]::IsNullOrWhiteSpace($relative) -or $relative -eq '.') {
                    continue
                }

                $pathBytes = [System.Text.Encoding]::UTF8.GetBytes($relative)
                $writer.Write([byte]0)
                $writer.Write([UInt32]$pathBytes.Length)
                $writer.Write($pathBytes)
                $writer.Write([UInt64]0)
            }

            $buffer = [byte[]]::new(1024 * 256)
            foreach ($file in $files) {
                $relative = Get-PortableRelativePath -Root $sourceFullPath -Path $file.FullName
                $pathBytes = [System.Text.Encoding]::UTF8.GetBytes($relative)
                $writer.Write([byte]1)
                $writer.Write([UInt32]$pathBytes.Length)
                $writer.Write($pathBytes)
                $writer.Write([UInt64]$file.Length)

                $input = [System.IO.File]::OpenRead($file.FullName)
                try {
                    while (($read = $input.Read($buffer, 0, $buffer.Length)) -gt 0) {
                        $writer.Write($buffer, 0, $read)
                    }
                }
                finally {
                    $input.Dispose()
                }
            }
        }
        finally {
            $writer.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }

    $package = Get-Item -LiteralPath $PackagePath
    Write-Host "Created installer payload: $($package.FullName) ($([Math]::Round($package.Length / 1MB, 2)) MB)"
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

$publishSingleFile = $publishSelfContained -and (-not $LooseFiles)

if (-not (Test-Path -LiteralPath (Join-Path $BackendSource 'CMakeLists.txt'))) {
    throw "Backend CMake project was not found at '$BackendSource'."
}

if (-not (Test-Path -LiteralPath $FrontendProject)) {
    throw "Frontend project was not found at '$FrontendProject'."
}

if (-not (Test-Path -LiteralPath $InstallerProject)) {
    throw "Installer project was not found at '$InstallerProject'."
}

Assert-ChildPath -Path $OutputDir -ParentPath $ProjectRoot
Assert-ChildPath -Path $InstallerOutputDir -ParentPath $ProjectRoot
Assert-ChildPath -Path $InstallerPayloadPath -ParentPath $ProjectRoot

Invoke-BuildStep "Preparing output folders" {
    if ((Test-Path -LiteralPath $OutputDir) -and (-not $NoClean)) {
        Remove-Item -LiteralPath $OutputDir -Recurse -Force
    }

    if ((Test-Path -LiteralPath $InstallerOutputDir) -and (-not $NoClean)) {
        Remove-Item -LiteralPath $InstallerOutputDir -Recurse -Force
    }

    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $OutputDir 'logs') -Force | Out-Null
    New-Item -ItemType Directory -Path $InstallerOutputDir -Force | Out-Null
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

    if ($publishSingleFile) {
        $publishArgs += @(
            '-p:PublishSingleFile=true',
            '-p:IncludeNativeLibrariesForSelfExtract=true',
            '-p:DebugType=none',
            '-p:DebugSymbols=false'
        )
    }

    & dotnet @publishArgs
}

Invoke-BuildStep "Copying native backend to output" {
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

$appExePath = Join-Path $OutputDir $FrontendExecutableName
if (-not (Test-Path -LiteralPath $appExePath)) {
    throw "Build completed, but $FrontendExecutableName was not found in '$OutputDir'."
}

Invoke-BuildStep "Creating installer payload" {
    Write-FluxoraPayloadPackage -SourceDirectory $OutputDir -PackagePath $InstallerPayloadPath
}

Invoke-BuildStep "Publishing Fluxora installer ($Configuration)" {
    $nativeInstallerCorePath = Get-NativeInstallerCorePath
    Write-Host "Using native installer core: $nativeInstallerCorePath"

    $installerPublishArgs = @(
        'publish',
        $InstallerProject,
        '--configuration',
        $Configuration,
        '--output',
        $InstallerOutputDir,
        '--self-contained',
        'true',
        '-p:PublishSingleFile=true',
        '-p:IncludeNativeLibrariesForSelfExtract=true',
        '-p:DebugType=none',
        '-p:DebugSymbols=false'
    )

    if (-not [string]::IsNullOrWhiteSpace($Runtime)) {
        $installerPublishArgs += @('--runtime', $Runtime)
    }

    & dotnet @installerPublishArgs

    $setupExePath = Join-Path $InstallerOutputDir 'FluxoraSetup.exe'
    if (-not (Test-Path -LiteralPath $setupExePath)) {
        throw "Installer publish completed, but FluxoraSetup.exe was not found in '$InstallerOutputDir'."
    }

    Get-ChildItem -LiteralPath $InstallerOutputDir -Filter '*.pdb' -File -ErrorAction SilentlyContinue |
        Remove-Item -Force
}

Write-Host ""
Write-Host "Done. Project outputs are ready:"
Write-Host "  Portable:  $OutputDir"
Write-Host "  Installer: $InstallerOutputDir"
