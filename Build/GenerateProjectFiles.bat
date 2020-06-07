@echo off
setlocal enabledelayedexpansion

set LAUNCH_VS=1
set SOLUTION_FILE=VQE.sln
set SOLUTION_DIRECTORY=SolutionFiles

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
echo [VQBuild] Checking pre-requisites...

:: Check if CMake is installed
cmake --version > nul 2>&1
if %errorlevel% NEQ 0 (
    echo [VQBuild] Cannot find path to cmake. Is CMake installed? Exiting...
    exit /b -1
) else (
    echo [VQBuild]   CMake            - Ready.
)

:: Check if submodule is initialized to avoid CMake file not found errors
call :CheckAndInitializeSubmodules

echo.

:: Generate Build directory
if not exist !SOLUTION_DIRECTORY! (
    echo [VQBuild] Creating directory !SOLUTION_DIRECTORY!...
    mkdir !SOLUTION_DIRECTORY!
)

:: Run CMake
cd !SOLUTION_DIRECTORY!

echo [VQBuild] Generating solution files...

cmake ..\.. -G "Visual Studio 16 2019" -A x64


if !errorlevel! EQU 0 (
    echo [VQBuild] Success!
    if !LAUNCH_VS! EQU 1 (
        start %SOLUTION_FILE%
    )
) else (
    echo.
    echo [VQBuild] GenerateSolutions.bat: Error with CMake. No solution file generated. 
    echo [VQBuild] Retrying without specifying VS 2019...
    rmdir /S /Q SolutionFiles
    cmake ..\..
    if !errorlevel! NEQ 0 (
        echo [VQBuild] GenerateSolutions.bat: Error with CMake. No solution file generated after retrying. 
        exit /b -1
    )
    echo.
)

echo.
cd ..
exit /b 0


:: -----------------------------------------------------------------------------------------------


::
:: CheckAndInitializeSubmodules()
::
:CheckAndInitializeSubmodules
set SUBMODULE_DIR=..\Libs\VQUtils\
set SUBMODULE_FILE=CMakeLists.txt
set SUBMODULE_FILE_PATH=!SUBMODULE_DIR!!SUBMODULE_FILE!
if not exist !SUBMODULE_FILE_PATH! (
    echo    [VQBuild] Git Submodules   - Not Ready: File !SUBMODULE_FILE! doesn't exist in '!SUBMODULE_DIR!'  
    echo    [VQBuild] Initializing submodule...

    :: attempt to initialize submodule
    cd ..
    echo.
    git submodule init 
    git submodule update
    cd Build

    :: check if submodule initialized properly
    if not exist !SUBMODULE_FILE_PATH! (
        echo.
        echo [VQBuild] Could not initialize submodule. Make sure all the submodules are initialized and updated.
        echo [VQBuild] Exiting...
        echo.
        exit /b -1 
    ) else (
        echo    [VQBuild] Git Submodules   - Ready.
    )
) else (
    echo [VQBuild]   Git Submodules   - Ready.
)
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