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

if !errorlevel! NEQ 0 (
    echo [VQBuild] Error checking submodules. Exiting.
    exit /b -1
)

echo.

:: Generate Build directory
if not exist !SOLUTION_DIRECTORY! (
    echo [VQBuild] Creating directory !SOLUTION_DIRECTORY!...
    mkdir !SOLUTION_DIRECTORY!
)

:: Run CMake
cd !SOLUTION_DIRECTORY!

echo [VQBuild] Generating solution files...

call :RunCmake

echo.
cd ..
exit /b !errorlevel!


:: -----------------------------------------------------------------------------------------------


::
:: CheckAndInitializeSubmodules()
::
:CheckAndInitializeSubmodules
set SUBMODULE_FILE=CMakeLists.txt

set SUBMODULE_DIR0=..\Libs\VQUtils\
set SUBMODULE_DIR1=..\Libs\D3D12MA\
set SUBMODULE_DIR2=..\Source\Renderer\Libs\D3DX12\
set SUBMODULE_DIR3=..\Libs\imgui\
set SUBMODULE_FILE_PATH0=!SUBMODULE_DIR0!!SUBMODULE_FILE!
set SUBMODULE_FILE_PATH1=!SUBMODULE_DIR1!!SUBMODULE_FILE!
set SUBMODULE_FILE_PATH2=!SUBMODULE_DIR2!
set SUBMODULE_FILE_PATH3=!SUBMODULE_DIR3!!SUBMODULE_FILE!

:: walk thru submodule paths
set MISSING_SUBMODULE_DIRS=
set NEED_TO_INIT_SUBMODULES=0
if not exist !SUBMODULE_FILE_PATH0! ( 
    set NEED_TO_INIT_SUBMODULES=1 
    set MISSING_SUBMODULE_DIRS=!SUBMODULE_DIR0!,
)
if not exist !SUBMODULE_FILE_PATH1! ( 
    set NEED_TO_INIT_SUBMODULES=1 
    set MISSING_SUBMODULE_DIRS=!MISSING_SUBMODULE_DIRS! !SUBMODULE_DIR1!,
)
if not exist !SUBMODULE_FILE_PATH2! ( 
    set NEED_TO_INIT_SUBMODULES=1 
    set MISSING_SUBMODULE_DIRS=!MISSING_SUBMODULE_DIRS! !SUBMODULE_DIR2!,
)
if not exist !SUBMODULE_FILE_PATH3! ( 
    set NEED_TO_INIT_SUBMODULES=1 
    set MISSING_SUBMODULE_DIRS=!MISSING_SUBMODULE_DIRS! !SUBMODULE_DIR3!,
)

:: init submodules if necessary
if !NEED_TO_INIT_SUBMODULES! neq 0 (
    echo [VQBuild]    Git Submodules   - Not Ready: File !SUBMODULE_FILE! doesn't exist in !MISSING_SUBMODULE_DIRS!
    echo [VQBuild]    Initializing submodule...

    :: attempt to initialize submodule
    cd ..
    echo.
    git submodule update --init Libs/D3D12MA
    git submodule update --init Libs/VQUtils
    git submodule update --init Libs/assimp
    git submodule update --init Libs/imgui
    git submodule update --init Source/Renderer/Libs/D3DX12
    cd Build

    :: check if submodule initialized properly
    set NEED_TO_INIT_SUBMODULES=0
    if not exist !SUBMODULE_FILE_PATH0! ( set NEED_TO_INIT_SUBMODULES=1 )
    if not exist !SUBMODULE_FILE_PATH1! ( set NEED_TO_INIT_SUBMODULES=1 )
    if not exist !SUBMODULE_FILE_PATH2! ( set NEED_TO_INIT_SUBMODULES=1 )
    if not exist !SUBMODULE_FILE_PATH3! ( set NEED_TO_INIT_SUBMODULES=1 )
    if !NEED_TO_INIT_SUBMODULES! neq 0 (
        echo.
        echo [VQBuild]    Could not initialize submodule. Make sure all the submodules are initialized and updated.
        echo [VQBuild]    Exiting...
        echo.
        exit /b -1 
    ) else (
        echo [VQBuild]    Git Submodules   - Ready.
    )
) else (
    echo [VQBuild]   Git Submodules   - Ready.
)
exit /b 0


::
:: RunCmake()
::
:RunCmake

:: assimp importers
set ASSIMP_IMPORT_FORMATS=-DASSIMP_BUILD_OBJ_IMPORTER=TRUE
set ASSIMP_IMPORT_FORMATS=!ASSIMP_IMPORT_FORMATS! -DASSIMP_BUILD_GLTF_IMPORTER=TRUE
:: assimp build options
set CMAKE_ASSIMP_PARAMETERS=-DASSIMP_BUILD_ASSIMP_TOOLS=OFF
set CMAKE_ASSIMP_PARAMETERS=!CMAKE_ASSIMP_PARAMETERS! -DASSIMP_NO_EXPORT=ON 
set CMAKE_ASSIMP_PARAMETERS=!CMAKE_ASSIMP_PARAMETERS! -DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=FALSE
set CMAKE_ASSIMP_PARAMETERS=!CMAKE_ASSIMP_PARAMETERS! -DBUILD_SHARED_LIBS=OFF 
set CMAKE_ASSIMP_PARAMETERS=!CMAKE_ASSIMP_PARAMETERS! -DASSIMP_BUILD_TESTS=OFF 
set CMAKE_ASSIMP_PARAMETERS=!CMAKE_ASSIMP_PARAMETERS! -DASSIMP_INSTALL=OFF
set CMAKE_ASSIMP_PARAMETERS=!CMAKE_ASSIMP_PARAMETERS! !ASSIMP_IMPORT_FORMATS!


cmake ..\.. -G "Visual Studio 16 2019" -A x64 !CMAKE_ASSIMP_PARAMETERS!

if !errorlevel! EQU 0 (
    echo [VQBuild] Success!
    if !LAUNCH_VS! EQU 1 (
        start %SOLUTION_FILE%
    )
) else (
    echo.
    echo [VQBuild] cmake VS2019 failed, retrying with VS 2017...
    echo [VQBuild] removing %~dp0SolutionFiles ...
    rmdir /S /Q  %~dp0SolutionFiles
    cmake ..\.. -G "Visual Studio 15 2017" -A x64 !CMAKE_ASSIMP_PARAMETERS!
    if !errorlevel! NEQ 0 (
        echo [VQBuild] cmake VS2017 failed, retrying without specifying VS version...
        echo [VQBuild] removing %~dp0SolutionFiles ...
        rmdir /S /Q  %~dp0SolutionFiles
        cmake ..\..
        if !errorlevel! NEQ 0 (
            echo [VQBuild] GenerateSolutions.bat: Error with CMake. No solution file generated after retrying. 
            exit /b -1
        )
    )
    echo. 
)

exit /b 0

