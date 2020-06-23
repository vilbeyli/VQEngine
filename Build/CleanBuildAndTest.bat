@echo off

setlocal enabledelayedexpansion

call PackageEngine.bat -Debug -Clean
cd ../Scripts
call TestVQE.bat -Debug