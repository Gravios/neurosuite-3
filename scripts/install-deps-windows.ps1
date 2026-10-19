<#
.SYNOPSIS
  Installs everything neurosuite-3 needs to build on Windows, and writes a
  CMakeUserPresets.json so the build is `cmake --preset windows`.

.DESCRIPTION
  Each step is skipped when what it installs is already there, so the script
  can be run again at any time (after a failure, or to add -WithHdf5 later).

    1. Git                         winget Git.Git
    2. Visual Studio 2022 Build    winget Microsoft.VisualStudio.2022.BuildTools
       Tools, C++ workload         (MSVC, Windows SDK, CMake, Ninja); skipped
                                   when any Visual Studio 2022 with the C++
                                   tools is installed
    3. Python 3                    winget Python.Python.3.12 (for the Qt download)
    4. vcpkg and its libraries     yaml-cpp, GSL, libxml2, libsamplerate;
                                   HDF5 / FFmpeg with -WithHdf5 / -WithFfmpeg
    5. Qt 6 for MSVC               aqtinstall (no Qt account needed)
    6. CMakeUserPresets.json       presets `windows` (MSVC) and `windows-sycl`
                                   (Intel icx), in the repository root

  The Intel oneAPI toolkit (for the Intel GPU / SYCL build) is not installed
  here; see doc/BUILD.md.

  Run it from cmd with scripts\install-deps-windows.bat (no execution-policy
  change needed), or from PowerShell:
    powershell -ExecutionPolicy Bypass -File scripts\install-deps-windows.ps1
  Installing Visual Studio asks for administrator rights (a UAC prompt).

.PARAMETER VcpkgRoot      vcpkg directory (default: %VCPKG_ROOT%, else C:\vcpkg)
.PARAMETER QtRoot         where Qt is installed (default: C:\Qt)
.PARAMETER QtVersion      Qt version (default: 6.8.3)
.PARAMETER WithHdf5       also install HDF5 (process_aomconvert)
.PARAMETER WithFfmpeg     also install FFmpeg (process_extractleds; a long build)
.PARAMETER WithPythonPackages  also pip-install PyYAML, NumPy, SciPy (pipeline helpers)
.PARAMETER DepsOnly       only vcpkg and Qt (steps 4-5): used by -DNS_INSTALL_DEPS=ON
.PARAMETER SkipQt         do not install Qt (one is already found elsewhere)
.PARAMETER NoPresets      do not write CMakeUserPresets.json
.PARAMETER Force          overwrite an existing CMakeUserPresets.json
.PARAMETER DryRun         print what would be run, change nothing

SPDX-License-Identifier: GPL-3.0-or-later
#>
[CmdletBinding()]
param(
    [string]$VcpkgRoot = '',
    [string]$QtRoot = 'C:\Qt',
    [string]$QtVersion = '6.8.3',
    [switch]$WithHdf5,
    [switch]$WithFfmpeg,
    [switch]$WithPythonPackages,
    [switch]$DepsOnly,
    [switch]$SkipQt,
    [switch]$NoPresets,
    [switch]$Force,
    [switch]$DryRun
)

# Windows PowerShell 5.1 compatible: no ternaries, no ??, no && in scripts;
# ASCII only (5.1 reads a file without a byte-order mark as ANSI).
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$RepoRoot = Split-Path -Parent $PSScriptRoot

# The vcpkg that ships inside Visual Studio (VC\vcpkg, which the developer
# prompts put in VCPKG_ROOT) has no classic mode: `vcpkg install <port>` fails
# there with "Could not locate a manifest".  It is recognised by its
# vcpkg-bundle.json, or by sitting inside the Visual Studio tree, and a
# separate vcpkg is used instead.
function Test-VsBundledVcpkg([string]$root) {
    if (-not $root) { return $false }
    if (Test-Path ([IO.Path]::Combine($root, 'vcpkg-bundle.json'))) { return $true }
    return ($root -match '[\\/]Microsoft Visual Studio[\\/]')
}
if (-not $VcpkgRoot) {
    if ($env:VCPKG_ROOT -and -not (Test-VsBundledVcpkg $env:VCPKG_ROOT)) { $VcpkgRoot = $env:VCPKG_ROOT }
    else { $VcpkgRoot = 'C:\vcpkg' }
}
if (Test-VsBundledVcpkg $VcpkgRoot) {
    Write-Host "[warn] $VcpkgRoot is the vcpkg bundled with Visual Studio, which cannot install packages outside a vcpkg.json manifest; using C:\vcpkg instead" -ForegroundColor Yellow
    $VcpkgRoot = 'C:\vcpkg'
}
$Triplet = 'x64-windows'
$QtArch  = 'win64_msvc2022_64'
$QtDir   = [IO.Path]::Combine($QtRoot, $QtVersion, 'msvc2022_64')
$NeedNewPrompt = $false

function Write-Step([string]$msg) { Write-Host "[deps] $msg" -ForegroundColor Cyan }
function Write-Ok([string]$msg)   { Write-Host "[ ok ] $msg" -ForegroundColor Green }
function Write-Warn2([string]$msg){ Write-Host "[warn] $msg" -ForegroundColor Yellow }

# Runs a program; stops the script with its exit code on failure.
function Invoke-Checked([string]$exe, [string[]]$arguments) {
    Write-Host ("       > " + $exe + " " + ($arguments -join ' ')) -ForegroundColor DarkGray
    if ($DryRun) { return }
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "'$exe $($arguments -join ' ')' failed with exit code $LASTEXITCODE"
    }
}

# winget installs do not update this session's PATH; read it back.
function Update-SessionPath {
    # Appended, not replaced: a developer prompt's own PATH entries stay.
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user    = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$env:Path;$machine;$user"
}

function Test-Command([string]$name) {
    return [bool](Get-Command $name -ErrorAction SilentlyContinue)
}

function Install-Winget([string]$id, [string[]]$extra = @()) {
    if (-not (Test-Command 'winget')) {
        throw "winget is not available. Install 'App Installer' from the Microsoft Store, or install $id by hand, then run this script again."
    }
    $wargs = @('install', '--id', $id, '-e', '--source', 'winget',
               '--accept-package-agreements', '--accept-source-agreements') + $extra
    Invoke-Checked 'winget' $wargs
    Update-SessionPath
}

# A Python 3 interpreter that is not the Microsoft Store placeholder.
function Find-Python {
    if (Test-Command 'py') {
        & py -3 -c "import sys" 2>$null
        if ($LASTEXITCODE -eq 0) { return @('py', '-3') }
    }
    $cmd = Get-Command 'python' -ErrorAction SilentlyContinue
    if ($cmd -and ($cmd.Source -notmatch 'WindowsApps')) {
        & python -c "import sys" 2>$null
        if ($LASTEXITCODE -eq 0) { return @('python') }
    }
    return $null
}

function Invoke-Python([string[]]$py, [string[]]$arguments) {
    $exe = $py[0]
    $pre = @()
    if ($py.Count -gt 1) { $pre = $py[1..($py.Count - 1)] }
    Invoke-Checked $exe ($pre + $arguments)
}

if (($env:OS -ne 'Windows_NT') -and -not $DryRun) {
    throw 'This script installs Windows dependencies; run it on Windows (or with -DryRun).'
}

Write-Step "repository : $RepoRoot"
Write-Step "vcpkg      : $VcpkgRoot ($Triplet)"
Write-Step "Qt         : $QtDir"
if ($DryRun) { Write-Warn2 'dry run: nothing is installed or written' }

# -- 1. Git --------------------------------------------------------------------
if (-not $DepsOnly) {
    if (Test-Command 'git') { Write-Ok 'Git found' }
    else {
        Write-Step 'installing Git'
        Install-Winget 'Git.Git'
    }
}
if (-not (Test-Command 'git') -and -not $DryRun) {
    $gitDefault = [IO.Path]::Combine($env:ProgramFiles, 'Git\cmd')
    if (Test-Path ([IO.Path]::Combine($gitDefault, 'git.exe'))) { $env:Path = "$gitDefault;$env:Path" }
}

# -- 2. Visual Studio 2022 Build Tools (C++) ----------------------------------
if (-not $DepsOnly) {
    $pf86 = ${env:ProgramFiles(x86)}
    if (-not $pf86) { $pf86 = 'C:\Program Files (x86)' }
    $vswhere = [IO.Path]::Combine($pf86, 'Microsoft Visual Studio\Installer\vswhere.exe')
    $vsPath = $null
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products '*' -version '[17.0,18.0)' `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
    }
    if ($vsPath) { Write-Ok "Visual Studio C++ tools found: $vsPath" }
    else {
        Write-Step 'installing Visual Studio 2022 Build Tools with the C++ workload (several GB; a UAC prompt follows)'
        $vsArgs = '--wait --passive --norestart ' +
                  '--add Microsoft.VisualStudio.Workload.VCTools ' +
                  '--add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ' +
                  '--add Microsoft.VisualStudio.Component.VC.CMake.Project ' +
                  '--add Microsoft.VisualStudio.Component.Windows11SDK.22621 ' +
                  '--includeRecommended'
        Install-Winget 'Microsoft.VisualStudio.2022.BuildTools' @('--override', $vsArgs)
        $NeedNewPrompt = $true
    }
}

# -- 3. Python 3 (only to download Qt, and for -WithPythonPackages) ----------
$qtConfig = [IO.Path]::Combine($QtDir, 'lib\cmake\Qt6\Qt6Config.cmake')
$needQt = (-not $SkipQt) -and -not (Test-Path $qtConfig)
$py = $null
if ($needQt -or $WithPythonPackages) { $py = Find-Python }
if ((-not $needQt) -and (-not $WithPythonPackages)) { }
elseif ($py) { Write-Ok ("Python found: " + ($py -join ' ')) }
elseif ($DepsOnly) {
    throw 'Python 3 is needed to download Qt (aqtinstall). Install it (winget install Python.Python.3.12) and configure again.'
}
else {
    Write-Step 'installing Python 3.12'
    Install-Winget 'Python.Python.3.12'
    $py = Find-Python
    if (-not $py) {
        if ($DryRun) { $py = @('py', '-3') }
        else { throw 'Python was installed but is not on PATH yet: open a new prompt and run this script again.' }
    }
}

# -- 4. vcpkg and the libraries -----------------------------------------------
$vcpkgExe = [IO.Path]::Combine($VcpkgRoot, 'vcpkg.exe')
if (Test-Path $vcpkgExe) { Write-Ok "vcpkg found: $vcpkgExe" }
else {
    Write-Step "setting up vcpkg in $VcpkgRoot"
    if (-not (Test-Path ([IO.Path]::Combine($VcpkgRoot, '.git')))) {
        Invoke-Checked 'git' @('clone', 'https://github.com/microsoft/vcpkg.git', $VcpkgRoot)
    }
    Invoke-Checked ([IO.Path]::Combine($VcpkgRoot, 'bootstrap-vcpkg.bat')) @('-disableMetrics')
}
$ports = @('yaml-cpp', 'gsl', 'libxml2', 'libsamplerate')
if ($WithHdf5)   { $ports += 'hdf5' }
if ($WithFfmpeg) { $ports += 'ffmpeg' }
# --vcpkg-root: the developer prompts' VCPKG_ROOT may still name Visual Studio's copy.
$portArgs = @('install', '--vcpkg-root', $VcpkgRoot) + ($ports | ForEach-Object { "${_}:$Triplet" })
Write-Step ("vcpkg libraries: " + ($ports -join ', ') + ' (built from source the first time; FFmpeg takes long)')
Push-Location $VcpkgRoot -ErrorAction SilentlyContinue
try { Invoke-Checked $vcpkgExe $portArgs } finally { Pop-Location -ErrorAction SilentlyContinue }

# -- 5. Qt --------------------------------------------------------------------
if ($SkipQt) { Write-Ok 'Qt: using the one already found' }
elseif (-not $needQt) { Write-Ok "Qt $QtVersion found: $QtDir" }
else {
    Write-Step "installing Qt $QtVersion ($QtArch) with aqtinstall"
    Invoke-Python $py @('-m', 'pip', 'install', '--user', '--upgrade', '--disable-pip-version-check', 'aqtinstall')
    # aqt writes aqtinstall.log into the current directory: keep it out of the repository.
    Push-Location ([IO.Path]::GetTempPath())
    try { Invoke-Python $py @('-m', 'aqt', 'install-qt', 'windows', 'desktop', $QtVersion, $QtArch, '-O', $QtRoot) }
    finally { Pop-Location }
}

if ($WithPythonPackages) {
    Write-Step 'Python packages for the pipeline helpers: PyYAML, NumPy, SciPy'
    Invoke-Python $py @('-m', 'pip', 'install', '--user', '--upgrade', '--disable-pip-version-check', 'pyyaml', 'numpy', 'scipy')
}

# -- 6. CMakeUserPresets.json -------------------------------------------------
if (-not $DepsOnly -and -not $NoPresets) {
    $presetsPath = [IO.Path]::Combine($RepoRoot, 'CMakeUserPresets.json')
    if ((Test-Path $presetsPath) -and -not $Force) {
        Write-Warn2 "$presetsPath exists; left as it is (-Force to overwrite)"
    }
    else {
        $toolchain = ([IO.Path]::Combine($VcpkgRoot, 'scripts\buildsystems\vcpkg.cmake')) -replace '\\', '/'
        $qtPrefix  = $QtDir -replace '\\', '/'
        $common = [ordered]@{
            CMAKE_BUILD_TYPE        = 'Release'
            CMAKE_TOOLCHAIN_FILE    = $toolchain
            VCPKG_TARGET_TRIPLET    = $Triplet
            CMAKE_PREFIX_PATH       = $qtPrefix
            USE_CUDA                = 'OFF'
            USE_HIP                 = 'OFF'
            NS_SKIP_KILOKLUSTAKWIK  = 'ON'
        }
        $msvc = [ordered]@{}; foreach ($k in $common.Keys) { $msvc[$k] = $common[$k] }
        $msvc['USE_SYCL'] = 'OFF'
        $sycl = [ordered]@{}; foreach ($k in $common.Keys) { $sycl[$k] = $common[$k] }
        $sycl['USE_SYCL'] = 'ON'
        $sycl['CMAKE_C_COMPILER'] = 'icx'
        $sycl['CMAKE_CXX_COMPILER'] = 'icx'
        $presets = [ordered]@{
            version = 3
            cmakeMinimumRequired = [ordered]@{ major = 3; minor = 21; patch = 0 }
            configurePresets = @(
                [ordered]@{
                    name = 'windows'
                    displayName = 'Windows, MSVC (run from the x64 Native Tools prompt)'
                    generator = 'Ninja'
                    binaryDir = '${sourceDir}/build'
                    installDir = 'C:/ns3'
                    cacheVariables = $msvc
                },
                [ordered]@{
                    name = 'windows-sycl'
                    displayName = 'Windows, Intel icx + SYCL (run from the oneAPI prompt)'
                    generator = 'Ninja'
                    binaryDir = '${sourceDir}/build-sycl'
                    installDir = 'C:/ns3-sycl'
                    cacheVariables = $sycl
                }
            )
            buildPresets = @(
                [ordered]@{ name = 'windows'; configurePreset = 'windows' },
                [ordered]@{ name = 'windows-sycl'; configurePreset = 'windows-sycl' }
            )
        }
        $json = $presets | ConvertTo-Json -Depth 10
        Write-Step "writing $presetsPath"
        if (-not $DryRun) {
            [IO.File]::WriteAllText($presetsPath, $json + "`n", (New-Object Text.UTF8Encoding($false)))
        } else {
            Write-Host $json -ForegroundColor DarkGray
        }
    }
}

# -- Done ---------------------------------------------------------------------
Write-Host ''
Write-Ok 'dependencies are in place'
if ($DepsOnly) { return }
if ($NeedNewPrompt) {
    Write-Warn2 'Visual Studio was just installed: open a NEW "x64 Native Tools Command Prompt for VS 2022".'
}
Write-Host @"

Next, in the "x64 Native Tools Command Prompt for VS 2022", in $RepoRoot :

    cmake --preset windows
    cmake --build --preset windows
    cmake --install build

The programs are then in C:\ns3\bin, with their DLLs.  For the Intel GPU
build use the oneAPI prompt and the windows-sycl preset instead.
"@
