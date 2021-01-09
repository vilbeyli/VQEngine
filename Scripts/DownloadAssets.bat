@echo off

setlocal enabledelayedexpansion

::-------------------------------------------------------------------------------------------------------------------------------------------------------------
set SEVEN_ZIP_PATH=%~dp0../Tools/7z.exe
set WGET_PATH=%~dp0../Tools/wget.exe
pushd %~dp0
cd ../Data/Textures/HDRI
set HDRI_TEXTURES_DESTINATION_PATH=%cd%/
cd ../PBR
set PBR_TEXTURES_DESTINATION_PATH=%cd%/
popd
::-------------------------------------------------------------------------------------------------------------------------------------------------------------
:: HDRI Files
set HDRI_WEB_PATH=https://hdrihaven.com/files/hdris/
set HDRI_FILE_LIST=venice_sunset_8k.hdr
set HDRI_FILE_LIST=%HDRI_FILE_LIST%;gym_entrance_8k.hdr
set HDRI_FILE_LIST=%HDRI_FILE_LIST%;shanghai_bund_8k.hdr
set HDRI_FILE_LIST=%HDRI_FILE_LIST%;stadium_01_8k.hdr
set HDRI_FILE_LIST=%HDRI_FILE_LIST%;sunny_vondelpark_8k.hdr
set HDRI_FILE_LIST=%HDRI_FILE_LIST%;winter_evening_8k.hdr
set HDRI_DOWNLOADED_FILES=
:: PBR Files
set PBR_WEB_PATH=https://cdn.cgbookcase.cloud/file/cgbookcase/textures/downloads/
set PBR_FILE_LIST=Metal_tiles_03/Metal_tiles_03_2K_Base_Color.png
set PBR_FILE_LIST=%PBR_FILE_LIST%;Metal_tiles_03/Metal_tiles_03_2K_Roughness.png
set PBR_FILE_LIST=%PBR_FILE_LIST%;Metal_tiles_03/Metal_tiles_03_2K_Metallic.png
set PBR_FILE_LIST=%PBR_FILE_LIST%;Metal_tiles_03/Metal_tiles_03_2K_Normal.png
set PBR_FILE_LIST=%PBR_FILE_LIST%;Painted_metal_02/Painted_metal_02_2K_Base_Color.png
set PBR_FILE_LIST=%PBR_FILE_LIST%;Painted_metal_02/Painted_metal_02_2K_Metallic.png
set PBR_FILE_LIST=%PBR_FILE_LIST%;Painted_metal_02/Painted_metal_02_2K_Normal.png
set PBR_FILE_LIST=%PBR_FILE_LIST%;Painted_metal_02/Painted_metal_02_2K_Roughness.png
set PBR_FILE_LIST=%PBR_FILE_LIST%;Marble_08/Marble_08_2K_Roughness.png
set PBR_FILE_LIST=%PBR_FILE_LIST%;Marble_08/Marble_08_2K_Normal.png
set PBR_FILE_LIST=%PBR_FILE_LIST%;Marble_08/Marble_08_2K_Base_Color.png
set PBR_FILE_LIST=%PBR_FILE_LIST%;Small_tiles_01/Small_tiles_01_2K_Roughness.png
set PBR_FILE_LIST=%PBR_FILE_LIST%;Small_tiles_01/Small_tiles_01_2K_Normal.png
set PBR_FILE_LIST=%PBR_FILE_LIST%;Small_tiles_01/Small_tiles_01_2K_Base_Color.png
set PBR_FILE_LIST=%PBR_FILE_LIST%;Black_herringbone_tiles_01/Black_herringbone_tiles_01_2K_Roughness.png
set PBR_FILE_LIST=%PBR_FILE_LIST%;Black_herringbone_tiles_01/Black_herringbone_tiles_01_2K_Normal.png
set PBR_FILE_LIST=%PBR_FILE_LIST%;Black_herringbone_tiles_01/Black_herringbone_tiles_01_2K_Base_Color.png
set PBR_FILE_LIST=%PBR_FILE_LIST%;Black_herringbone_tiles_01/Black_herringbone_tiles_01_2K_AO.png
set PBR_DOWNLOADED_FILES=
::-------------------------------------------------------------------------------------------------------------------------------------------------------------
:: parameter scan
for %%i IN (%*) DO (
  if "%%i"=="-TODO" set TODO=1
)
::-------------------------------------------------------------------------------------------------------------------------------------------------------------

::
:: MAIN()
::
:: Download the HDR textures that doesn't already exist using wget and store them in the HDRI_DOWNLOADED_FILES 'list'
set ALL_ASSETS_ALREADY_DOWNLOADED=1
for %%f in (%HDRI_FILE_LIST%) do ( 
  if not exist %HDRI_TEXTURES_DESTINATION_PATH%%%f (
    echo MISSING HDRI FILES!!!!
    %WGET_PATH% %HDRI_WEB_PATH%%%f -P %HDRI_TEXTURES_DESTINATION_PATH%
    set ALL_ASSETS_ALREADY_DOWNLOADED=0
    set HDRI_DOWNLOADED_FILES=!HDRI_DOWNLOADED_FILES!;%HDRI_TEXTURES_DESTINATION_PATH%%%f
  )
)
:: print output status msg
if !ALL_ASSETS_ALREADY_DOWNLOADED! equ 1 (
  echo All HDRI assets already downloaded.
) else (
  echo.
  echo Downloaded HDRI textures to: !HDRI_TEXTURES_DESTINATION_PATH!
  for %%f in (%HDRI_DOWNLOADED_FILES%) do (
    echo  - %%f
  )
)

:: Download the PBR textures that doesn't already exist using wget and store them in the PBR_DOWNLOADED_FILES 'list'
set ALL_ASSETS_ALREADY_DOWNLOADED=1
for %%f in (%PBR_FILE_LIST%) do ( 
  if not exist %PBR_TEXTURES_DESTINATION_PATH%%%f (
    %WGET_PATH% %PBR_WEB_PATH%%%f -O %PBR_TEXTURES_DESTINATION_PATH%%%f
    set ALL_ASSETS_ALREADY_DOWNLOADED=0
    set PBR_DOWNLOADED_FILES=!PBR_DOWNLOADED_FILES!;%PBR_TEXTURES_DESTINATION_PATH%%%f
  )
)
if !ALL_ASSETS_ALREADY_DOWNLOADED! equ 1 (
  echo All PBR assets already downloaded.
) else (
  echo.
  echo Downloaded PBR textures to: !PBR_TEXTURES_DESTINATION_PATH!
  for %%f in (%PBR_DOWNLOADED_FILES%) do (
    echo  - %%f
  )
)

git submodule update --init ../Data/Models

exit /b 0
