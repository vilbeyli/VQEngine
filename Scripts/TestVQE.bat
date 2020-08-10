@echo off

setlocal enabledelayedexpansion

::-------------------------------------------------------------------------------------------------------------------------------------------------------------
cd ../Build/_artifacts
set DIR_BUILD=!CD!

set VQE_REL_DIR=!DIR_BUILD!
set VQE_DBG_DIR=!DIR_BUILD!
set VQE_RWD_DIR=!DIR_BUILD!

set RUN_TEST_REL=1
set RUN_TEST_DBG=0
set RUN_TEST_RWD=0
::-------------------------------------------------------------------------------------------------------------------------------------------------------------

:: parameter scan
for %%i IN (%*) DO (
  if "%%i"=="-debug" set RUN_TEST_DBG=1
  if "%%i"=="-Debug" set RUN_TEST_DBG=1
  if "%%i"=="-D"     set RUN_TEST_DBG=1

  if "%%i"=="-RWD"              set RUN_TEST_RWD=1
  if "%%i"=="-RelWithDebugInfo" set RUN_TEST_RWD=1
)

::-------------------------------------------------------------------------------------------------------------------------------------------------------------

::
:: MAIN()
::
echo.
echo [VQTest] RUNNING TESTS...

call :CheckDirectories

if !errorlevel! NEQ 0 (
    echo [VQTest] Couldn't find the executables to test.
    exit /b -1 
)

if !RUN_TEST_DBG! NEQ 0  call :RunTest !VQE_DBG_DIR!, VQE-d.exe
if !RUN_TEST_REL! NEQ 0  call :RunTest !VQE_REL_DIR!, VQE.exe
if !RUN_TEST_RWD! NEQ 0  call :RunTest !VQE_RWD_DIR!, VQE-rwd.exe

exit /b 0



::
::Check Dir and File
:: 
:CheckDirAndFile
set DIR=%~1
set FILE=%~2
if not exist !DIR! (
    echo [VQTest] Error: !DIR! doesn't exist.
    exit /b -1
)
set FILE_PATH=!DIR!/!FILE!
if not exist !FILE_PATH! (
    echo [VQTest] Error: !FILE_PATH! doesn't exist.
)
exit /b 0

::
:: Check directories
::
:CheckDirectories
set DIR_NOT_EXIST=0
call :CheckDirAndFile !VQE_REL_DIR!, !VQE_REL_EXE!
set DIR_NOT_EXIST=!errorlevel!

if !RUN_TEST_DBG! NEQ 0 (
    call :CheckDirAndFile !VQE_DBG_DIR!, !VQE_DBG_EXE!
    set DIR_NOT_EXIST=!errorlevel!
)
if !RUN_TEST_RWD! NEQ 0 (
    call :CheckDirAndFile !VQE_RWD_DIR!, !VQE_RWD_EXE!
    set DIR_NOT_EXIST=!errorlevel!
)
if !DIR_NOT_EXIST! NEQ 0 exit /b -1
exit /b 0


:: 
:: Run Test
::
:RunTest
set TEST_EXE_PATH=%~1/%~2
cd %~1
echo **************************************************************************
echo [VQTest] Running Test: !TEST_EXE_PATH!

call !TEST_EXE_PATH! -Test -LogConsole -LogFile

set TEST_RET_CODE=!errorlevel!
if !TEST_RET_CODE! NEQ 0 (
    echo [VQTest] Error: !TEST_RET_CODE!
) else (
    echo [VQTest] Success!
)

echo **************************************************************************
exit /b !TEST_REST_CODE!