@echo off

setlocal enabledelayedexpansion

::-------------------------------------------------------------------------------------------------------------------------------------------------------------
set SEVEN_ZIP_PATH=%~dp0../Tools/7z.exe
set HDRI_TEXTURES_DESTINATION_PATH=%~dp0../Data/Textures/HDRI/
::-------------------------------------------------------------------------------------------------------------------------------------------------------------

:: parameter scan
for %%i IN (%*) DO (
  if "%%i"=="-TODO" set TODO=1
)

::-------------------------------------------------------------------------------------------------------------------------------------------------------------

::
:: MAIN()
::
echo.
echo [VQ] Downloading Assets...

git clone https://github.com/vilbeyli/VQ-HDR
cd VQ-HDR

call :ExtractHDRI

cd ..

:: clean up
echo Deleting files in VQ-HDR...
del "VQ-HDR" /s /q /f
echo Removing VQ-HDR...
rmdir "./VQ-HDR" /s /q

exit /b 0



::
:: ExtractHDRI()
::
:ExtractHDRI 
cd HDRI/
echo 7z=!SEVEN_ZIP_PATH!
!SEVEN_ZIP_PATH! e HDRI.7z A
robocopy ./ !HDRI_TEXTURES_DESTINATION_PATH! /Move *.hdr *.exr 
cd ..
exit /b 0

