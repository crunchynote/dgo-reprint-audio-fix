@echo off
setlocal
rem Removes the DGO reprint route audio fix and restores the game's original ArrowheadUSBIO.dll.

set "HERE=%~dp0"
set "DLLDIR="
if exist "%HERE%TG4AC\Binaries\Win64\dll\" set "DLLDIR=%HERE%TG4AC\Binaries\Win64\dll"
if not defined DLLDIR if exist "%HERE%DGOREPR\TG4AC\Binaries\Win64\dll\" set "DLLDIR=%HERE%DGOREPR\TG4AC\Binaries\Win64\dll"

if not defined DLLDIR (
    echo Could not find TG4AC\Binaries\Win64\dll.
    echo Run uninstall.bat from the same folder you ran install.bat from.
    goto :fail
)
if not exist "%DLLDIR%\ArrowheadUSBIO_orig.dll" (
    echo The fix is not installed ^(ArrowheadUSBIO_orig.dll not found^).
    goto :fail
)

del /f "%DLLDIR%\ArrowheadUSBIO.dll" 2>nul
if exist "%DLLDIR%\ArrowheadUSBIO.dll" (
    echo Could not remove ArrowheadUSBIO.dll. Close the game and try again.
    goto :fail
)
ren "%DLLDIR%\ArrowheadUSBIO_orig.dll" ArrowheadUSBIO.dll || goto :fail
del /f "%DLLDIR%\ArrowheadUSBIO_volume.ini" 2>nul

echo Fix removed. The game's original ArrowheadUSBIO.dll is back in place.
pause
exit /b 0

:fail
echo.
echo Nothing was changed.
pause
exit /b 1
