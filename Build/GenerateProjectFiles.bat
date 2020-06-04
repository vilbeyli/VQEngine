@echo off
setlocal enabledelayedexpansion

set LAUNCH_VS=1
set SOLUTION_FILE=VQE.sln
set SOLUTION_DIRECTORY=VQE

:: parameter scan
for %%i IN (%*) DO (

    if "%%i"=="-noVS" set LAUNCH_VS=0

    if "%%i"=="help" call :PrintUsage
    if "%%i"=="-h"   call :PrintUsage
    if "%%i"=="/h"   call :PrintUsage
)


::
:: Main() 
::
echo.
echo [VQE-Build] Checking pre-requisites...

:: Check if CMake is installed
cmake --version > nul 2>&1
if %errorlevel% NEQ 0 (
    echo [VQE-Build] Cannot find path to cmake. Is CMake installed? Exiting...
    exit /b -1
) else (
    echo [VQE-Build]   CMake            - Ready.
)

:: Check if submodule is initialized to avoid CMake file not found errors
call :CheckAndInitializeSubmodules

echo.

:: Generate Build directory
if not exist !SOLUTION_DIRECTORY! (
    echo [VQE-Build] Creating directory !SOLUTION_DIRECTORY!...
    mkdir !SOLUTION_DIRECTORY!
)

:: Run CMake
cd !SOLUTION_DIRECTORY!

echo [VQE-Build] Generating solution files...

cmake ..\..


if !errorlevel! EQU 0 (
    echo [VQE-Build] Success!
    if !LAUNCH_VS! EQU 1 (
        start %SOLUTION_FILE%
    )
) else (
    echo.
    echo [VQE-Build] GenerateSolutions.bat: Error with CMake. No solution file generated. 
    echo.
    pause
)

echo.
cd ..
exit /b 0


:: -----------------------------------------------------------------------------------------------


::
:: CheckAndInitializeSubmodules()
::
:CheckAndInitializeSubmodules
:: TODO: set & check submodules

::set SUBMODULE_DIR=..\Source\3rdParty\DirectXTex\
::set SUBMODULE_FILE=!SUBMODULE_DIR!DirectXTex_Desktop_2017.sln
::if not exist !SUBMODULE_FILE! (
::    echo    Git Submodules   - Not Ready: File common.cmake doesn't exist in '!SUBMODULE_DIR!'  -  Initializing submodule...
::
::    :: attempt to initialize submodule
::    cd ..
::    echo.
::    git submodule init
::    git submodule update
::    cd build
::
::    :: check if submodule initialized properly
::    if not exist !SUBMODULE_FILE! (
::        echo.
::        echo Could not initialize submodule. Make sure all the submodules are initialized and updated.
::        echo Exiting...
::        echo.
::        exit /b -1 
::    ) else (
::        echo    Git Submodules   - Ready.
::    )
::) else (
    echo [VQE-Build]   Git Submodules   - Ready.
::)
exit /b 0



::
:: PrintUsage()
::
:PrintUsage
::echo Usage    : GenerateSolutions.bat [API]
::echo.
::echo Examples : GenerateSolutions.bat VK
::echo            GenerateSolutions.bat DX
::echo            GenerateSolutions.bat Vulkan
::echo            GenerateSolutions.bat DX12
exit /b 0f