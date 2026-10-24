@echo off
setlocal
rem Installs the DGO reprint route audio fix.
rem Put this folder's files in the game's DGOREPR folder (next to DgoRepr.exe), or in the
rem folder that contains DGOREPR, then run this script.

set "HERE=%~dp0"
set "DLLDIR="
if exist "%HERE%TG4AC\Binaries\Win64\dll\" set "DLLDIR=%HERE%TG4AC\Binaries\Win64\dll"
if not defined DLLDIR if exist "%HERE%DGOREPR\TG4AC\Binaries\Win64\dll\" set "DLLDIR=%HERE%DGOREPR\TG4AC\Binaries\Win64\dll"

if not defined DLLDIR (
    echo Could not find TG4AC\Binaries\Win64\dll.
    echo Put these files in the DGOREPR folder ^(next to DgoRepr.exe^) and run install.bat again.
    goto :fail
)
if not exist "%HERE%ArrowheadUSBIO.dll" (
    echo ArrowheadUSBIO.dll is missing from this folder.
    goto :fail
)

if exist "%DLLDIR%\ArrowheadUSBIO_orig.dll" (
    echo Fix already installed, updating it.
) else (
    if not exist "%DLLDIR%\ArrowheadUSBIO.dll" (
        echo The game's original ArrowheadUSBIO.dll was not found in:
        echo   %DLLDIR%
        goto :fail
    )
    ren "%DLLDIR%\ArrowheadUSBIO.dll" ArrowheadUSBIO_orig.dll || goto :fail
)

copy /y "%HERE%ArrowheadUSBIO.dll" "%DLLDIR%\ArrowheadUSBIO.dll" >nul || goto :fail
if not exist "%DLLDIR%\ArrowheadUSBIO_volume.ini" (
    copy /y "%HERE%ArrowheadUSBIO_volume.ini" "%DLLDIR%\ArrowheadUSBIO_volume.ini" >nul
)

echo.
echo Installed to:
echo   %DLLDIR%
echo Change the volume in ArrowheadUSBIO_volume.ini there. Run uninstall.bat to remove the fix.
pause
exit /b 0

:fail
echo.
echo Nothing was changed.
pause
exit /b 1
