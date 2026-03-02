@echo off
setlocal EnableDelayedExpansion
:: =============================================================================
:: build-neurosuite.bat
:: Build and install all NeuroSuite-3 packages on Windows (MSVC + CMake).
::
:: Build order (dependency graph):
::   1. nphys-data          -- data files only, no compiler needed
::   2. libklustersshared   -- shared Qt6 library; all Qt apps depend on it
::   3. klusters            -- depends on libklustersshared
::   4. neuroscope          -- depends on libklustersshared
::   5. ndmanager           -- depends on libklustersshared
::   6. ndmanager-plugins   -- standalone C/C++ (no Qt); optional FFmpeg/CUDA
::   7. klustakwik          -- standalone C/C++; optional CUDA/HIP/SYCL
::   8. spikerealign        -- standalone C/C++; optional CUDA/HIP/SYCL
::
:: Usage:
::   build-neurosuite.bat [OPTIONS]
::
:: Options:
::   --prefix DIR          Install prefix            (default: C:\NeuroSuite)
::   --build-dir DIR       Build tree parent          (default: .\build)
::   --source-dir DIR      Source tree parent         (default: .\src)
::   --jobs N              Parallel build jobs        (default: %NUMBER_OF_PROCESSORS%)
::   --qt-dir DIR          Qt6 cmake dir, e.g.
::                         C:\Qt\6.7.0\msvc2022_64\lib\cmake\Qt6
::   --vcpkg-root DIR      vcpkg root                 (default: %VCPKG_ROOT% or C:\vcpkg)
::   --no-install          Build but do not install
::   --no-vcpkg            Skip automatic vcpkg dependency installation
::   --with-ffmpeg         Install FFmpeg via vcpkg (needed by process_extractleds)
::   --with-libsamplerate  Install libsamplerate via vcpkg (recommended over bundled copy)
::   --cuda-arch LIST      Semicolon-separated CUDA arch list (e.g. "75;86;89")
::   --skip PKG            Skip a package (repeat for multiple). Valid names:
::                         nphys-data libklustersshared klusters neuroscope
::                         ndmanager ndmanager-plugins klustakwik spikerealign
::   --gpu-off             Disable all GPU backends (CUDA / HIP / SYCL)
::   --clean               Delete each package's build directory after install
::   -h / --help           Show this help
::
:: Requirements:
::   - Visual Studio 2022 with "Desktop development with C++" workload
::   - CMake 3.21+ (bundled with VS, or from https://cmake.org/download/)
::   - Git for Windows (https://git-scm.com/download/win)
::   - Qt 6.6+ installed via Qt Online Installer -- set --qt-dir or Qt6_DIR env var
::   - vcpkg bootstrapped at %VCPKG_ROOT% or --vcpkg-root
::
:: Run this script from a "Developer Command Prompt for VS 2022" so that
:: cl.exe and the MSVC toolchain are on PATH.
:: =============================================================================

:: ── ANSI colour helpers (Windows 10 1511+ / Windows Terminal) ────────────────
for /f "delims=" %%A in ('echo prompt $E ^| cmd /q') do set "ESC=%%A"
set "RESET=%ESC%[0m"
set "BOLD=%ESC%[1m"
set "CYAN=%ESC%[36m"
set "GREEN=%ESC%[32m"
set "YELLOW=%ESC%[33m"
set "RED=%ESC%[31m"

:: ── Defaults ─────────────────────────────────────────────────────────────────
set "PREFIX=C:\NeuroSuite"
set "BUILD_BASE=%~dp0build"
set "SOURCE_BASE=%~dp0src"
set "QT_DIR="
set "VCPKG_ROOT_ARG="
set "JOBS="
set "DO_INSTALL=1"
set "DO_VCPKG=1"
set "WITH_FFMPEG=0"
set "WITH_LIBSAMPLERATE=0"
set "CUDA_ARCH="
set "GPU_OFF=0"
set "CLEAN=0"
set "SKIP_LIST= "

:: ── Argument parsing ─────────────────────────────────────────────────────────
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="--prefix"             ( set "PREFIX=%~2"           & shift & shift & goto parse_args )
if /i "%~1"=="--build-dir"          ( set "BUILD_BASE=%~2"       & shift & shift & goto parse_args )
if /i "%~1"=="--source-dir"         ( set "SOURCE_BASE=%~2"      & shift & shift & goto parse_args )
if /i "%~1"=="--jobs"               ( set "JOBS=%~2"             & shift & shift & goto parse_args )
if /i "%~1"=="--qt-dir"             ( set "QT_DIR=%~2"           & shift & shift & goto parse_args )
if /i "%~1"=="--vcpkg-root"         ( set "VCPKG_ROOT_ARG=%~2"   & shift & shift & goto parse_args )
if /i "%~1"=="--cuda-arch"          ( set "CUDA_ARCH=%~2"        & shift & shift & goto parse_args )
if /i "%~1"=="--skip"               ( set "SKIP_LIST=!SKIP_LIST!%~2 " & shift & shift & goto parse_args )
if /i "%~1"=="--no-install"         ( set "DO_INSTALL=0"         & shift & goto parse_args )
if /i "%~1"=="--no-vcpkg"           ( set "DO_VCPKG=0"           & shift & goto parse_args )
if /i "%~1"=="--with-ffmpeg"        ( set "WITH_FFMPEG=1"        & shift & goto parse_args )
if /i "%~1"=="--with-libsamplerate" ( set "WITH_LIBSAMPLERATE=1" & shift & goto parse_args )
if /i "%~1"=="--gpu-off"            ( set "GPU_OFF=1"            & shift & goto parse_args )
if /i "%~1"=="--clean"              ( set "CLEAN=1"              & shift & goto parse_args )
if /i "%~1"=="-h"                   goto show_help
if /i "%~1"=="--help"               goto show_help
echo %RED%[FAIL ]%RESET% Unknown option: %~1
exit /b 1

:show_help
:: Print lines that begin with "::" from the header block
set "_IN_HELP=0"
for /f "usebackq delims=" %%L in ("%~f0") do (
    set "_LINE=%%L"
    if "!_LINE:~0,2!"=="::" (
        set "_IN_HELP=1"
        set "_TEXT=!_LINE:~3!"
        echo !_TEXT!
    ) else (
        if !_IN_HELP!==1 goto help_done
    )
)
:help_done
exit /b 0

:args_done

:: ── Resolve vcpkg root ───────────────────────────────────────────────────────
if not "%VCPKG_ROOT_ARG%"=="" (
    set "VCPKG=%VCPKG_ROOT_ARG%"
) else if defined VCPKG_ROOT (
    set "VCPKG=%VCPKG_ROOT%"
) else (
    set "VCPKG=C:\vcpkg"
)

:: ── Resolve parallel jobs ────────────────────────────────────────────────────
if "%JOBS%"=="" (
    set "JOBS=%NUMBER_OF_PROCESSORS%"
    if "!JOBS!"=="" set "JOBS=4"
)

:: ── Banner ───────────────────────────────────────────────────────────────────
echo.
echo %BOLD%=================================================%RESET%
echo %BOLD%  NeuroSuite-3  Windows Build Script%RESET%
echo %BOLD%=================================================%RESET%
echo %CYAN%[build]%RESET% Source dir  : %SOURCE_BASE%
echo %CYAN%[build]%RESET% Build dir   : %BUILD_BASE%
echo %CYAN%[build]%RESET% Install to  : %PREFIX%
echo %CYAN%[build]%RESET% Jobs        : %JOBS%
echo %CYAN%[build]%RESET% vcpkg root  : %VCPKG%
echo %CYAN%[build]%RESET% Install     : %DO_INSTALL%
if not "%QT_DIR%"==""    echo %CYAN%[build]%RESET% Qt6_DIR     : %QT_DIR%
if not "%CUDA_ARCH%"=="" echo %CYAN%[build]%RESET% CUDA arch   : %CUDA_ARCH%
if %GPU_OFF%==1          echo %CYAN%[build]%RESET% GPU backends: disabled (--gpu-off)
if not "%SKIP_LIST%"==" " echo %YELLOW%[ WARN]%RESET% Skipping    :%SKIP_LIST%

:: ── Pre-flight checks ────────────────────────────────────────────────────────
echo.
echo %CYAN%[build]%RESET% Checking prerequisites...

where cmake >nul 2>&1
if errorlevel 1 (
    echo %RED%[FAIL ]%RESET% cmake not found.
    echo          Install Visual Studio 2022 with CMake Tools, or download from:
    echo          https://cmake.org/download/  (check "Add to PATH" during install)
    exit /b 1
)
echo %GREEN%[  OK ]%RESET% cmake found.

where git >nul 2>&1
if errorlevel 1 (
    echo %RED%[FAIL ]%RESET% git not found. Install from: https://git-scm.com/download/win
    exit /b 1
)
echo %GREEN%[  OK ]%RESET% git found.

:: Warn if not inside a VS Developer Prompt (cl.exe not on PATH)
where cl >nul 2>&1
if errorlevel 1 (
    echo %YELLOW%[ WARN]%RESET% cl.exe not found on PATH.
    echo          CMake may fail to find MSVC. For best results, run this script from:
    echo          Start menu ^> "Developer Command Prompt for VS 2022"
) else (
    echo %GREEN%[  OK ]%RESET% MSVC cl.exe found.
)

:: Verify all source trees are present
echo %CYAN%[build]%RESET% Verifying source trees...
set "_MISSING=0"
for %%P in (nphys-data libklustersshared klusters neuroscope ndmanager ndmanager-plugins klustakwik spikerealign) do (
    if not exist "%SOURCE_BASE%\%%P\CMakeLists.txt" (
        echo %RED%[FAIL ]%RESET% Source tree not found: %SOURCE_BASE%\%%P
        set "_MISSING=1"
    )
)
if %_MISSING%==1 (
    echo.
    echo          Run this script from the neurosuite-3 repository root, or pass:
    echo          --source-dir ^<path-to-repo^>\src
    exit /b 1
)
echo %GREEN%[  OK ]%RESET% All source trees found.

:: vcpkg presence check
if %DO_VCPKG%==1 (
    if not exist "%VCPKG%\vcpkg.exe" (
        echo %YELLOW%[ WARN]%RESET% vcpkg not found at: %VCPKG%
        echo          To set up vcpkg:
        echo            git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
        echo            C:\vcpkg\bootstrap-vcpkg.bat
        echo          Then re-run with --vcpkg-root C:\vcpkg  (or set VCPKG_ROOT)
        echo          Skipping vcpkg dep install; pass --no-vcpkg to suppress this warning.
        set "DO_VCPKG=0"
    )
)

:: Qt6_DIR resolution: CLI arg beats env var beats nothing
if "%QT_DIR%"=="" (
    if defined Qt6_DIR (
        set "QT_DIR=%Qt6_DIR%"
        echo %CYAN%[build]%RESET% Qt6_DIR from environment: !QT_DIR!
    ) else (
        echo %YELLOW%[ WARN]%RESET% Qt6_DIR not set. CMake will search default paths.
        echo          If configure fails, re-run with:
        echo            --qt-dir C:\Qt\6.7.0\msvc2022_64\lib\cmake\Qt6
    )
)

:: Build base directory
if not exist "%BUILD_BASE%" mkdir "%BUILD_BASE%"

:: =============================================================================
:: vcpkg: install required and optional libraries
:: =============================================================================
if %DO_VCPKG%==1 (
    echo.
    echo %BOLD%=================================================%RESET%
    echo %BOLD%  Installing vcpkg dependencies%RESET%
    echo %BOLD%=================================================%RESET%

    :: Always required
    set "VCPKG_PKGS=yaml-cpp:x64-windows libxml2:x64-windows gsl:x64-windows"

    if %WITH_LIBSAMPLERATE%==1 set "VCPKG_PKGS=!VCPKG_PKGS! libsamplerate:x64-windows"
    if %WITH_FFMPEG%==1        set "VCPKG_PKGS=!VCPKG_PKGS! ffmpeg:x64-windows"

    echo %CYAN%[build]%RESET% Packages : !VCPKG_PKGS!
    "%VCPKG%\vcpkg.exe" install !VCPKG_PKGS!
    if errorlevel 1 (
        echo %RED%[FAIL ]%RESET% vcpkg install failed. See output above.
        exit /b 1
    )
    echo %GREEN%[  OK ]%RESET% vcpkg dependencies installed.

    if %WITH_FFMPEG%==0 (
        echo %YELLOW%[ WARN]%RESET% FFmpeg not requested. The ndmanager-plugins plugin
        echo          process_extractleds will NOT be built (it requires FFmpeg).
        echo          Pass --with-ffmpeg if you need LED tracking support.
    )
    if %WITH_LIBSAMPLERATE%==0 (
        echo %YELLOW%[ WARN]%RESET% libsamplerate not requested. process_resample will attempt
        echo          to compile the vendored copy (libsamplerate-0.1.8). This requires
        echo          a bash shell + autoconf and will likely FAIL on native Windows.
        echo          Pass --with-libsamplerate to avoid this.
    )
)

:: =============================================================================
:: Compose reusable flag strings
:: =============================================================================
set "TOOLCHAIN_FLAG="
if exist "%VCPKG%\scripts\buildsystems\vcpkg.cmake" (
    set "TOOLCHAIN_FLAG=-DCMAKE_TOOLCHAIN_FILE=%VCPKG%\scripts\buildsystems\vcpkg.cmake"
)

set "QT_FLAG="
if not "%QT_DIR%"=="" set "QT_FLAG=-DQt6_DIR=%QT_DIR%"

set "GPU_FLAGS="
if %GPU_OFF%==1 set "GPU_FLAGS=-DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF"

set "CUDA_ARCH_FLAG="
if not "%CUDA_ARCH%"=="" set "CUDA_ARCH_FLAG=-DCMAKE_CUDA_ARCHITECTURES=%CUDA_ARCH%"

:: =============================================================================
:: :cmake_build  LABEL  SOURCE_DIR  [extra cmake -D flags...]
::
:: Uses GOTO-based argument passing because batch subroutines receive
:: shifted copies of %1..%9 only; we collect extra flags into _EXTRA.
:: =============================================================================
goto :begin_builds

:cmake_build
    set "_LABEL=%~1"
    set "_SRC=%~2"
    shift & shift

    :: Collect any remaining arguments as extra CMake -D flags
    set "_EXTRA="
    :extra_loop
        if "%~1"=="" goto extra_done
        set "_EXTRA=!_EXTRA! %~1"
        shift
        goto extra_loop
    :extra_done

    :: ── Skip check ───────────────────────────────────────────────────────────
    echo !SKIP_LIST! | findstr /i /c:" !_LABEL! " >nul 2>&1
    if not errorlevel 1 (
        echo %YELLOW%[ WARN]%RESET% Skipping !_LABEL! (--skip requested^)
        goto :cmake_build_ret
    )

    set "_BUILD=%BUILD_BASE%\!_LABEL!"

    echo.
    echo %BOLD%=================================================%RESET%
    echo %BOLD%  !_LABEL!%RESET%
    echo %BOLD%=================================================%RESET%
    echo %CYAN%[build]%RESET% Source : !_SRC!
    echo %CYAN%[build]%RESET% Build  : !_BUILD!
    echo %CYAN%[build]%RESET% Prefix : %PREFIX%

    if not exist "!_BUILD!" mkdir "!_BUILD!"

    :: ── Configure ────────────────────────────────────────────────────────────
    echo %CYAN%[build]%RESET% Configuring...
    cmake -S "!_SRC!" -B "!_BUILD!" ^
        -DCMAKE_INSTALL_PREFIX="%PREFIX%" ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_PREFIX_PATH="%PREFIX%" ^
        %TOOLCHAIN_FLAG% ^
        %QT_FLAG% ^
        !_EXTRA!
    if errorlevel 1 (
        echo %RED%[FAIL ]%RESET% CMake configure failed for !_LABEL!.
        exit /b 1
    )

    :: ── Compile ──────────────────────────────────────────────────────────────
    echo %CYAN%[build]%RESET% Compiling (jobs: %JOBS%)...
    cmake --build "!_BUILD!" --config Release --parallel %JOBS%
    if errorlevel 1 (
        echo %RED%[FAIL ]%RESET% Compile failed for !_LABEL!.
        exit /b 1
    )

    :: ── Install ──────────────────────────────────────────────────────────────
    if %DO_INSTALL%==1 (
        echo %CYAN%[build]%RESET% Installing...
        cmake --install "!_BUILD!" --config Release
        if errorlevel 1 (
            echo %RED%[FAIL ]%RESET% Install failed for !_LABEL!.
            exit /b 1
        )
        echo %GREEN%[  OK ]%RESET% !_LABEL! installed to %PREFIX%.
    ) else (
        echo %GREEN%[  OK ]%RESET% !_LABEL! built (install skipped^).
    )

    :: ── Clean ────────────────────────────────────────────────────────────────
    if %CLEAN%==1 if %DO_INSTALL%==1 (
        echo %CYAN%[build]%RESET% Removing build tree: !_BUILD!
        rmdir /s /q "!_BUILD!"
    )

    :cmake_build_ret
    exit /b 0

:: =============================================================================
:begin_builds
:: =============================================================================

:: ── 1. nphys-data ─────────────────────────────────────────────────────────────
:: No compiler required, no dependencies. Just installs MIME types and icons.
call :cmake_build "nphys-data" "%SOURCE_BASE%\nphys-data"
if errorlevel 1 exit /b 1

:: ── 2. libklustersshared ──────────────────────────────────────────────────────
:: Must be installed before klusters, neuroscope, and ndmanager.
:: Deps: Qt6 (Core, Gui, Widgets), yaml-cpp
call :cmake_build "libklustersshared" "%SOURCE_BASE%\libklustersshared"
if errorlevel 1 exit /b 1

:: ── 3. klusters ──────────────────────────────────────────────────────────────
:: Deps: Qt6 (Core, Gui, Widgets, Xml, PrintSupport), libklustersshared
:: Optional: OpenMP (bundled with MSVC), CUDA, HIP, SYCL
call :cmake_build "klusters" "%SOURCE_BASE%\klusters" %GPU_FLAGS% %CUDA_ARCH_FLAG%
if errorlevel 1 exit /b 1

:: ── 4. neuroscope ─────────────────────────────────────────────────────────────
:: Deps: Qt6 (Core, Gui, Widgets, Xml, PrintSupport), libklustersshared
call :cmake_build "neuroscope" "%SOURCE_BASE%\neuroscope"
if errorlevel 1 exit /b 1

:: ── 5. ndmanager ─────────────────────────────────────────────────────────────
:: Deps: Qt6 (Core, Gui, Widgets, Xml), libklustersshared
call :cmake_build "ndmanager" "%SOURCE_BASE%\ndmanager"
if errorlevel 1 exit /b 1

:: ── 6. ndmanager-plugins ─────────────────────────────────────────────────────
:: Deps: LibXml2 (required), GSL (required), OpenMP (optional), libsamplerate
::       (optional, bundled fallback), FFmpeg (optional, process_extractleds only)
:: No Qt6 or libklustersshared dependency.
call :cmake_build "ndmanager-plugins" "%SOURCE_BASE%\ndmanager-plugins" %CUDA_ARCH_FLAG%
if errorlevel 1 exit /b 1

:: ── 7. klustakwik ────────────────────────────────────────────────────────────
:: Deps: OpenMP (optional). Optional GPU: CUDA, HIP, SYCL (auto-detected).
:: NOTE: If Intel oneAPI is installed, CMake may auto-select icpx as compiler.
::       Pass --gpu-off (sets -DUSE_SYCL=OFF etc.) to force MSVC.
call :cmake_build "klustakwik" "%SOURCE_BASE%\klustakwik" %GPU_FLAGS% %CUDA_ARCH_FLAG%
if errorlevel 1 exit /b 1

:: ── 8. spikerealign ──────────────────────────────────────────────────────────
:: Deps: OpenMP (optional). Optional GPU: CUDA, HIP, SYCL (auto-detected).
call :cmake_build "spikerealign" "%SOURCE_BASE%\spikerealign" %GPU_FLAGS% %CUDA_ARCH_FLAG%
if errorlevel 1 exit /b 1

:: =============================================================================
:: Post-install summary
:: =============================================================================
echo.
echo %BOLD%=================================================%RESET%
echo %BOLD%  Build complete!%RESET%
echo %BOLD%=================================================%RESET%

if %DO_INSTALL%==1 (
    echo %GREEN%[  OK ]%RESET% All packages installed to: %PREFIX%
    echo.
    echo %CYAN%[build]%RESET% Next steps:
    echo.
    echo   1. Add NeuroSuite bin directory to your PATH (open a new terminal after):
    echo        setx PATH "%%PATH%%;%PREFIX%\bin"
    echo.
    echo   2. Ensure Qt runtime DLLs are visible. Either:
    echo      a) Add Qt bin to PATH (adjust Qt version/path):
    echo           setx PATH "%%PATH%%;C:\Qt\6.7.0\msvc2022_64\bin"
    echo      b) Or deploy DLLs alongside each executable:
    echo           windeployqt %PREFIX%\bin\klusters.exe
    echo           windeployqt %PREFIX%\bin\neuroscope.exe
    echo           windeployqt %PREFIX%\bin\ndmanager.exe
    echo.
    if %DO_VCPKG%==1 (
    echo   3. Ensure vcpkg runtime DLLs are on PATH:
    echo        setx PATH "%%PATH%%;%VCPKG%\installed\x64-windows\bin"
    echo      Tip: use the x64-windows-static triplet to avoid DLL dependencies:
    echo        vcpkg install yaml-cpp:x64-windows-static ...
    echo        (add -DVCPKG_TARGET_TRIPLET=x64-windows-static to cmake calls)
    echo.
    )
    echo   4. Open a new terminal and verify the installation:
    echo        klusters --version
    echo        neuroscope --version
    echo        ndmanager --version
    echo        KlustaKwik
    echo.
) else (
    echo %CYAN%[build]%RESET% Build trees are in: %BUILD_BASE%
    echo %CYAN%[build]%RESET% Re-run without --no-install to install.
)

endlocal
exit /b 0
