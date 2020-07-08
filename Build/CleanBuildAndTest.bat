@echo off

setlocal enabledelayedexpansion

call PackageEngine.bat -Debug -Clean -SkipExplorer
cd ../Scripts
call TestVQE.bat -Debug