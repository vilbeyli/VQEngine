@echo off

setlocal enabledelayedexpansion

::-------------------------------------------------------------------------------------------------------------------------------------------------------------

set VSWHERE="%PROGRAMFILES(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set MSBUILD_QUERY1=!VSWHERE! -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe
set MSBUILD_QUERY2=vswhere.exe -version "[15.0,16.0)" -products Microsoft.VisualStudio.Product.BuildTools -find MSBuild\**\Bin\MSBuild.exe
set MSBUILD_QUERY3=vswhere.exe -version "[15.0,16.0)" -find MSBuild\**\Bin\MSBuild.exe
set MSBUILD_FIND=1

set MSBUILD_QUERY=!MSBUILD_QUERY1!
set MSBUILD=MSBuild.exe

set SOLUTION_DIRECTORY=SolutionFiles\
set SOLUTION_FILE_NAME=VQE.sln
set SOLUTION_FILE_PATH=!SOLUTION_DIRECTORY!!SOLUTION_FILE_NAME!

set BUILD_CONFIG_DEBUG=0
set BUILD_CONFIG_RELEASE=1
set BUILD_CONFIG_REL_WITH_DBG=0

set BUILD_FLAG_CLEAN=0

set DBG_BUILD_DIRECTORY=../Bin/DEBUG
set RLS_BUILD_DIRECTORY=../Bin/RELEASE
set RWD_BUILD_DIRECTORY=../Bin/RELWITHDEBINFO
set SHADER_DIRECTORY=../Source/Shaders
set DATA_DIRECTORY=../Data

:: Keep track of # build tasks. build, clean, copy/move, etc.
:: assume 2 for build+copy, add more depending on prebuild+clean tasks
set BUILD_NUM_TASKS=2
set BUILD_NUM_CURR_TASK=0

::-------------------------------------------------------------------------------------------------------------------------------------------------------------


:: parameter scan
for %%i IN (%*) DO (
    if "%%i"=="-c"      call :AddBuildTask_Clean
    if "%%i"=="-clean"  call :AddBuildTask_Clean
    if "%%i"=="-Clean"  call :AddBuildTask_Clean
    if "%%i"=="-C"      call :AddBuildTask_Clean

    if "%%i"=="-DebugOnly" (
         set BUILD_CONFIG_DEBUG=1
         set BUILD_CONFIG_RELEASE=0
         set BUILD_CONFIG_REL_WITH_DBG=0
    )

    if "%%i"=="-Release" set BUILD_CONFIG_RELEASE=1
    if "%%i"=="-Debug"   (
        set BUILD_CONFIG_DEBUG=1
        set /A BUILD_NUM_TASKS=!BUILD_NUM_TASKS!+1
    )
    if "%%i"=="-RelWithDebInfo" (
        set BUILD_CONFIG_REL_WITH_DBG=1
        set /A BUILD_NUM_TASKS=!BUILD_NUM_TASKS!+1
    )

    if "%%i"=="-SkipMSBuildFind" set MSBUILD_FIND=0
    if "%%i"=="-SkipPackaging"   set BUILD_TASKS_ONLY=1
)

::echo SkipMSBuildFind=!MSBUILD_FIND!
if !MSBUILD_FIND! equ 1 (
    call :FindMSBuild
    if %ERRORLEVEL% neq 0 (
        echo [VQPackage] Error: Couldn't find MSBuild
        exit /b -1
    )
)
::-------------------------------------------------------------------------------------------------------------------------------------------------------------

set ENGINE_PACKAGE_OUTPUT_DIRECTORY=_artifacts
set ENGINE_BUILD_COMMAND="!MSBUILD!" "%~dp0!SOLUTION_FILE_PATH!"

::
:: MAIN()
::
echo.
echo [VQPackage] Packaging Engine...

pushd %~dp0


:: Check if GenerateProjectFiles.bat has been run
if not exist !SOLUTION_DIRECTORY! (
    echo [VQPackage] Solution directory '!SOLUTION_DIRECTORY!' doesn't exist.
    mkdir !SOLUTION_DIRECTORY!
)

call :ExecBuildTask_PreBuild

:: clean if specified
if !BUILD_FLAG_CLEAN! equ 1 (
    call :ExecBuildTask_Clean
)

:: Package the engine
call :ExecBuildTask_Build

if %ERRORLEVEL% neq 0  exit /b %ERRORLEVEL%

:: move build artifacts into destination folder
call :ExecBuildTask_Move
echo [VQPackage] PACKAGING SUCCESSFUL!
start !ENGINE_PACKAGE_OUTPUT_DIRECTORY!

popd


exit /b 0

::==============================================================================================================================
::==============================================================================================================================
::==============================================================================================================================

::
:: FindMSBuild()
::
:FindMSBuild

for /f "usebackq tokens=*" %%i IN (`%MSBUILD_QUERY%`) DO (
    set MSBUILD=%%i
    goto CHECK_MSBUILD
)
:CHECK_MSBUILD
if not exist !MSBUILD! (
    echo [VQPackage] Build Error: MSBuild.exe could not be located.
    echo.
    exit /b -1
)

:: check arg1 == true (bPrintMSBuild)
if "%~1"=="true" (
    echo [VQPackage] MSBuild Found: !MSBUILD!
)
exit /b 0

:: --------------------------------------------------------------------------

:: 
:: AddBuildTask_Clean()
:: 
:AddBuildTask_Clean
set BUILD_FLAG_CLEAN=1
set /A BUILD_NUM_TASKS=!BUILD_NUM_TASKS!+1
exit /b 0

:: --------------------------------------------------------------------------


::
:: ExecBuildTask_PreBuild()
::
:ExecBuildTask_PreBuild
if not exist !SOLUTION_FILE_PATH! (
    echo [VQPackage] Couldn't find !SOLUTION_FILE_PATH!
    echo.
    echo **********************************************************************
    echo        [!BUILD_NUM_CURR_TASK!/!BUILD_NUM_TASKS!] Running GenerateProjectFiles.bat...
    echo **********************************************************************
    echo.
    call %~dp0GenerateProjectFiles.bat -noVS
    if %ERRORLEVEL% neq 0 (
        echo [VQPackage] Error: Couldn't generate project files.
        exit /b -1
    )
)
set /A BUILD_NUM_CURR_TASK=!BUILD_NUM_CURR_TASK!+1
exit /b 0


::
:: ExecBuildTask_Clean()
::
:ExecBuildTask_Clean
call :PrintBuildStage Clean
!ENGINE_BUILD_COMMAND! /t:Clean /p:Configuration=Release
if !BUILD_CONFIG_DEBUG!        neq 0   !ENGINE_BUILD_COMMAND! /t:Clean /p:Configuration=Debug
if !BUILD_CONFIG_REL_WITH_DBG! neq 0   !ENGINE_BUILD_COMMAND! /t:Clean /p:Configuration=RelWithDebInfo 
set /A BUILD_NUM_CURR_TASK=!BUILD_NUM_CURR_TASK!+1
exit /b 0


::
:: ExecBuildTask_Build()
::
:ExecBuildTask_Build
::echo [VQPackage] ENGINE_BUILD_COMMAND = !ENGINE_BUILD_COMMAND!
:: ---------------------- Build Release ----------------------
call :PrintBuildStage Release
call !ENGINE_BUILD_COMMAND! /p:Configuration=Release
set /A ERR_REL=!ERRORLEVEL!
set /A BUILD_NUM_CURR_TASK=!BUILD_NUM_CURR_TASK!+1
:: ---------------------- Build Release ----------------------
:: ---------------------- Build Debug   ----------------------
if !BUILD_CONFIG_DEBUG! neq 0 (
    call :PrintBuildStage Debug
    call !ENGINE_BUILD_COMMAND! /p:Configuration=Debug
    set /A BUILD_NUM_CURR_TASK=!BUILD_NUM_CURR_TASK!+1
    if %ERRORLEVEL% neq 0 (
        echo ERROR: BUILD ERROR
        exit /b -1
    )
)
:: ---------------------- Build Debug   ----------------------
:: ---------------------- Build RelWithDebInfo----------------
if !BUILD_CONFIG_REL_WITH_DBG! neq 0 (
    call :PrintBuildStage RelWithDebInfo
    call !ENGINE_BUILD_COMMAND! /p:Configuration=RelWithDebInfo
    set /A ERR=!ERRORLEVEL!
    set /A BUILD_NUM_CURR_TASK=!BUILD_NUM_CURR_TASK!+1
    if !ERR! neq 0 (
        echo ERROR: BUILD ERROR
        exit /b -1
    )
)
:: ---------------------- Build RelWithDebInfo----------------
if !ERR_REL! neq 0 (
    echo ERROR: BUILD ERROR RELEASE
    exit /b -1
)

echo [VQPackage] BUILD SUCCESSFUL
echo               - Release
if !BUILD_CONFIG_DEBUG!        neq 0 echo               - Debug
if !BUILD_CONFIG_REL_WITH_DBG! neq 0 echo               - RelWithDbgInfo
exit /b 0




::
:: PackageBuild(source, dest)
::
:PackageBuild
set SRC=%~1
set DST=%~2
robocopy !SRC! !DST!
robocopy !SHADER_DIRECTORY! !DST!/Shaders
xcopy "!DATA_DIRECTORY!/EngineSettings.ini" "!DST!/Data"\ /Y /Q /F
exit /b 0

::
:: ExecBuildTask_Move()
::
:ExecBuildTask_Move
echo.
echo **********************************************************************
echo            [!BUILD_NUM_CURR_TASK!/!BUILD_NUM_TASKS!] Move artifacts 
echo **********************************************************************

:: Check engine packaging output directory and clean it if there's a previous engine package
if exist !ENGINE_PACKAGE_OUTPUT_DIRECTORY! (
    echo [VQPackage] Cleaning... ENGINE_PACKAGE_OUTPUT_DIRECTORY = !ENGINE_PACKAGE_OUTPUT_DIRECTORY!
    rmdir /S /Q !ENGINE_PACKAGE_OUTPUT_DIRECTORY!
)

:: make artifacts directory
mkdir !ENGINE_PACKAGE_OUTPUT_DIRECTORY!

:: move builds
echo [VQPackage] Moving build artifacts to package output directory...
call :PackageBuild !RLS_BUILD_DIRECTORY!, !ENGINE_PACKAGE_OUTPUT_DIRECTORY!/Win64
if !BUILD_CONFIG_DEBUG! NEQ 0         call :PackageBuild !DBG_BUILD_DIRECTORY!, !ENGINE_PACKAGE_OUTPUT_DIRECTORY!/Win64-Debug
if !BUILD_CONFIG_REL_WITH_DBG! NEQ 0  call :PackageBuild !RWD_BUILD_DIRECTORY!, !ENGINE_PACKAGE_OUTPUT_DIRECTORY!/Win64-PDB
exit /b 0

:: --------------------------------------------------------------------------

::
::
::
:PrintBuildStage
    echo.
    echo.
    echo **********************************************************************
    echo                [!BUILD_NUM_CURR_TASK!/!BUILD_NUM_TASKS!] Build %1
    echo **********************************************************************
exit /b 0