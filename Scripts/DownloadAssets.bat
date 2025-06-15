@echo off

setlocal enabledelayedexpansion

::-------------------------------------------------------------------------------------------------------------------------------------------------------------
set SEVEN_ZIP_PATH="%~dp0../Tools/7za.exe"
set WGET_PATH="%~dp0../Tools/wget.exe"
pushd "%~dp0"
cd "../Data/Textures/HDRI"
set HDRI_TEXTURES_DESTINATION_PATH=%cd%\
popd
::-------------------------------------------------------------------------------------------------------------------------------------------------------------
set MAIN_SCREEN_RESOLUTION_Y=0

:: HDRI Files
set HDRI_RESOLUTION=8k
set HDRI_WEB_PATH=https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/!HDRI_RESOLUTION!/
set HDRI_FILE_LIST=venice_sunset_!HDRI_RESOLUTION!.hdr
set HDRI_FILE_LIST=%HDRI_FILE_LIST%;gym_entrance_!HDRI_RESOLUTION!.hdr
set HDRI_FILE_LIST=%HDRI_FILE_LIST%;stadium_01_!HDRI_RESOLUTION!.hdr
set HDRI_FILE_LIST=%HDRI_FILE_LIST%;sunny_vondelpark_!HDRI_RESOLUTION!.hdr
set HDRI_DOWNLOADED_FILES=

::-------------------------------------------------------------------------------------------------------------------------------------------------------------
:: parameter scan
for %%i IN (%*) DO (
  if "%%i"=="-TODO" set TODO=1
)
::-------------------------------------------------------------------------------------------------------------------------------------------------------------

::
:: MAIN()
::
call :GetMainScreenResolution
::call :DetermineResolution_HDRI
::exit /b 0

:: Download the HDR textures that doesn't already exist using wget and store them in the HDRI_DOWNLOADED_FILES 'list'
set ALL_ASSETS_ALREADY_DOWNLOADED=1
set ERROR_DURING_DOWNLOAD=0
set ERROR_DURING_DOWNLOAD_FILE_LIST=

for %%f in (%HDRI_FILE_LIST%) do ( 
  if not exist %HDRI_TEXTURES_DESTINATION_PATH%%%f (
    %WGET_PATH% %HDRI_WEB_PATH%%%f -P %HDRI_TEXTURES_DESTINATION_PATH%
    if !ERRORLEVEL! neq 0 (
      echo ---------------------------------------------------------------------------
      echo ERROR downloading: %HDRI_WEB_PATH%%%f
      echo ---------------------------------------------------------------------------
      set ALL_ASSETS_ALREADY_DOWNLOADED=0
      set ERROR_DURING_DOWNLOAD=1
      set ERROR_DURING_DOWNLOAD_FILE_LIST=!ERROR_DURING_DOWNLOAD_FILE_LIST!;%HDRI_WEB_PATH%%%f
    ) else (
      set ALL_ASSETS_ALREADY_DOWNLOADED=0
      set HDRI_DOWNLOADED_FILES=!HDRI_DOWNLOADED_FILES!;%HDRI_TEXTURES_DESTINATION_PATH%%%f
    )
  )
)
:: print output status msg
if !ALL_ASSETS_ALREADY_DOWNLOADED! equ 1 (
  echo All HDRI assets already downloaded.
) else (
  if !ERROR_DURING_DOWNLOAD! equ 1 (
    echo.
    echo Failed downloading HDRI textures
    for %%f in (%ERROR_DURING_DOWNLOAD_FILE_LIST%) do (
      echo  - %%f
    )
    echo.
  ) else (
    echo.
    echo Downloaded HDRI textures to: !HDRI_TEXTURES_DESTINATION_PATH!
    for %%f in (%HDRI_DOWNLOADED_FILES%) do (
      echo  - %%f
    )
  )
)

:: Download the PBR textures & models
git submodule update --init ../Data/Models
git submodule update --init ../Data/Textures/PBR

exit /b 0


::
:: DetermineResolution_EnvironmentMaps()
::
:DetermineResolution_HDRI
if !MAIN_SCREEN_RESOLUTION_Y! geq 720  set HDRI_RESOLUTION=1k
if !MAIN_SCREEN_RESOLUTION_Y! geq 1080 set HDRI_RESOLUTION=2k
if !MAIN_SCREEN_RESOLUTION_Y! geq 1440 set HDRI_RESOLUTION=4k
if !MAIN_SCREEN_RESOLUTION_Y! geq 2160 set HDRI_RESOLUTION=8k
echo HDRI Resolution: !HDRI_RESOLUTION!
exit /b 0

::
:: GetMainScreenResolution()
::
:GetMainScreenResolution
:: https://stackoverflow.com/a/58515127/2034041
for /f delims^= %%I in ('
wmic.exe path Win32_VideoController get CurrentHorizontalResolution^,CurrentVerticalResolution^|findstr /b [0-9]
')do for /f tokens^=1-2 %%a in ('echo\%%I')do set "w=%%a" && set "h=%%b" && call echo=!w!x!h!
set MAIN_SCREEN_RESOLUTION_Y=!h!
if !MAIN_SCREEN_RESOLUTION_Y! lss 480 (
  echo Warning: MAIN_SCREEN_RESOLUTION_Y=!MAIN_SCREEN_RESOLUTION_Y! < 480
) 
exit /b 0