@echo off
setlocal enabledelayedexpansion
:: run_klustakwik.bat - Run KlustaKwik on all spike groups (Windows)
::
:: Sampling rate  : parsed from <basename>.yaml  (field: SamplingRate / samplingRate)
:: UseFeatures    : derived from column count in each .fet file
:: OMP_NUM_THREADS: derived from NUMBER_OF_PROCESSORS environment variable
::
:: Usage:
::   run_klustakwik.bat [basename] [options]
::
:: All KlustaKwik options can be overridden on the command line, e.g.:
::   run_klustakwik.bat jg05-20120316 -MaxClusters 16 -nStarts 3

:: ---------------------------------------------------------------------------
:: Defaults for every KlustaKwik option
:: ---------------------------------------------------------------------------
set OPT_MinClusters=5
set OPT_MaxClusters=12
set OPT_ChunkMinutes=10
set OPT_MergeThresh=60
set OPT_GlobalMergeIter=50
set OPT_TimeMergeIter=45
set OPT_PenaltyMix=0.0
set OPT_nStarts=1
set OPT_SplitEvery=40
set OPT_SaveIntermediates=0
set OPT_Log=1
set OPT_Screen=0
set BASENAME=

:: ---------------------------------------------------------------------------
:: Parse arguments
:: ---------------------------------------------------------------------------
:parse_args
if "%~1"=="" goto end_parse_args

if /i "%~1"=="-MinClusters"       ( set OPT_MinClusters=%~2       & shift & shift & goto parse_args )
if /i "%~1"=="-MaxClusters"       ( set OPT_MaxClusters=%~2       & shift & shift & goto parse_args )
if /i "%~1"=="-ChunkMinutes"      ( set OPT_ChunkMinutes=%~2      & shift & shift & goto parse_args )
if /i "%~1"=="-MergeThresh"       ( set OPT_MergeThresh=%~2       & shift & shift & goto parse_args )
if /i "%~1"=="-GlobalMergeIter"   ( set OPT_GlobalMergeIter=%~2   & shift & shift & goto parse_args )
if /i "%~1"=="-TimeMergeIter"     ( set OPT_TimeMergeIter=%~2     & shift & shift & goto parse_args )
if /i "%~1"=="-PenaltyMix"        ( set OPT_PenaltyMix=%~2        & shift & shift & goto parse_args )
if /i "%~1"=="-nStarts"           ( set OPT_nStarts=%~2           & shift & shift & goto parse_args )
if /i "%~1"=="-SplitEvery"        ( set OPT_SplitEvery=%~2        & shift & shift & goto parse_args )
if /i "%~1"=="-SaveIntermediates" ( set OPT_SaveIntermediates=%~2 & shift & shift & goto parse_args )
if /i "%~1"=="-Log"               ( set OPT_Log=%~2               & shift & shift & goto parse_args )
if /i "%~1"=="-Screen"            ( set OPT_Screen=%~2            & shift & shift & goto parse_args )

:: First non-flag argument is the basename
if "!BASENAME!"=="" (
    set BASENAME=%~1
    shift
    goto parse_args
)

echo Unknown argument: %~1 >&2
exit /b 1

:end_parse_args

:: Auto-detect basename from .xml if not supplied
if "!BASENAME!"=="" (
    for %%F in (*.xml) do (
        set BASENAME=%%~nF
        goto found_basename
    )
    echo Could not detect basename. Supply it as the first argument. >&2
    exit /b 1
    :found_basename
    echo No basename supplied - using detected: !BASENAME!
)

:: ---------------------------------------------------------------------------
:: Parse sampling rate from YAML
:: ---------------------------------------------------------------------------
set YAML_FILE=!BASENAME!.yaml
if not exist "!YAML_FILE!" (
    echo YAML file not found: !YAML_FILE! >&2
    exit /b 1
)

set SAMPLING_RATE=
for /f "usebackq tokens=1,* delims=:" %%A in ("!YAML_FILE!") do (
    set _KEY=%%A
    set _VAL=%%B
    :: Trim leading spaces from key
    set _KEY=!_KEY: =!
    :: Match SamplingRate / samplingRate / sampling_rate (case-insensitive via lowering)
    if /i "!_KEY!"=="SamplingRate"  ( call :trim_val "!_VAL!" & goto got_sr )
    if /i "!_KEY!"=="sampling_rate" ( call :trim_val "!_VAL!" & goto got_sr )
)
echo Could not parse SamplingRate from !YAML_FILE! >&2
exit /b 1
:got_sr
echo SamplingRate    = !SAMPLING_RATE!  (from !YAML_FILE!)

:: ---------------------------------------------------------------------------
:: OMP_NUM_THREADS from NUMBER_OF_PROCESSORS (Windows env var)
:: ---------------------------------------------------------------------------
set OMP_NUM_THREADS=%NUMBER_OF_PROCESSORS%
echo OMP_NUM_THREADS = !OMP_NUM_THREADS!  (from NUMBER_OF_PROCESSORS)

:: ---------------------------------------------------------------------------
:: Iterate over every spike group that has a .fet file
:: ---------------------------------------------------------------------------
set FOUND_ANY=0

for %%F in (!BASENAME!.fet.*) do (
    set FET_FILE=%%F
    :: Extract group suffix (everything after the last dot)
    set GROUP=%%~xF
    set GROUP=!GROUP:~1!

    if "!GROUP!"=="0" (
        echo Skipping group 0
    ) else (
        set FOUND_ANY=1

        :: Read first line of .fet file = total column count
        set TOTAL_COLS=
        for /f "usebackq delims=" %%L in ("%%F") do (
            if "!TOTAL_COLS!"=="" set TOTAL_COLS=%%L
        )
        :: Strip any carriage return
        set TOTAL_COLS=!TOTAL_COLS:~0,-1!
        if "!TOTAL_COLS:~-1!"==" " set TOTAL_COLS=!TOTAL_COLS:~0,-1!

        set /a N_FEATURES=!TOTAL_COLS! - 1

        if !N_FEATURES! LEQ 0 (
            echo Group !GROUP!: no features, skipping >&2
        ) else (
            :: Build UseFeatures string of N_FEATURES ones
            set USE_FEATURES=
            for /l %%I in (1,1,!N_FEATURES!) do set USE_FEATURES=!USE_FEATURES!1

            echo.
            echo === Group !GROUP! ^| features: !N_FEATURES! ^| UseFeatures: !USE_FEATURES! ===

            set OMP_NUM_THREADS=!OMP_NUM_THREADS!
            KlustaKwik "!BASENAME!" !GROUP! ^
                -MinClusters        !OPT_MinClusters! ^
                -MaxClusters        !OPT_MaxClusters! ^
                -UseFeatures        !USE_FEATURES! ^
                -ChunkMinutes       !OPT_ChunkMinutes! ^
                -SamplingRate       !SAMPLING_RATE! ^
                -MergeThresh        !OPT_MergeThresh! ^
                -GlobalMergeIter    !OPT_GlobalMergeIter! ^
                -TimeMergeIter      !OPT_TimeMergeIter! ^
                -PenaltyMix         !OPT_PenaltyMix! ^
                -nStarts            !OPT_nStarts! ^
                -SplitEvery         !OPT_SplitEvery! ^
                -SaveIntermediates  !OPT_SaveIntermediates! ^
                -Log                !OPT_Log! ^
                -Screen             !OPT_Screen!
        )
    )
)

if "!FOUND_ANY!"=="0" (
    echo No .fet files found for basename '!BASENAME!' >&2
    exit /b 1
)

echo.
echo Done.
endlocal
exit /b 0

:: ---------------------------------------------------------------------------
:: Subroutine: trim leading/trailing spaces and quotes from a value,
::             store result in SAMPLING_RATE
:: ---------------------------------------------------------------------------
:trim_val
set _RAW=%~1
:: Remove leading spaces
:trim_lead
if "!_RAW:~0,1!"==" " ( set _RAW=!_RAW:~1! & goto trim_lead )
:: Remove trailing spaces
:trim_trail
if "!_RAW:~-1!"==" " ( set _RAW=!_RAW:~0,-1! & goto trim_trail )
set SAMPLING_RATE=!_RAW!
exit /b 0
