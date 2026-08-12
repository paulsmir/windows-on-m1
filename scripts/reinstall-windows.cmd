@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Guarded Windows redeployment for WinPE on Apple Silicon.
rem The partition table is never modified. Volumes are identified by content
rem and exact labels because WinPE drive letters can change between boots.

set "SOURCE_DRIVE="
set "WINDOWS_DRIVE="
set "WINESP_DRIVE="
set /a SOURCE_COUNT=0
set /a WINDOWS_COUNT=0
set /a WINESP_COUNT=0

echo ============================================================
echo Guarded Windows reinstall - discovery only
echo ============================================================
echo Scanning mounted volumes...

for %%L in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do (
    if exist "%%L:\sources\install.wim" (
        set /a SOURCE_COUNT+=1
        set "SOURCE_DRIVE=%%L:"
    )
    for /f "delims=" %%V in ('vol %%L: 2^>nul ^| findstr /R /C:" is Windows$"') do (
        set /a WINDOWS_COUNT+=1
        set "WINDOWS_DRIVE=%%L:"
    )
    for /f "delims=" %%V in ('vol %%L: 2^>nul ^| findstr /R /C:" is WINESP$"') do (
        set /a WINESP_COUNT+=1
        set "WINESP_DRIVE=%%L:"
    )
)

if not "!SOURCE_COUNT!"=="1" (
    echo ERROR: expected exactly one volume containing sources\install.wim.
    echo Found: !SOURCE_COUNT!
    exit /b 10
)
if not "!WINDOWS_COUNT!"=="1" (
    echo ERROR: expected exactly one volume with the exact label Windows.
    echo Found: !WINDOWS_COUNT!
    exit /b 11
)
if not "!WINESP_COUNT!"=="1" (
    echo ERROR: expected exactly one volume with the exact label WINESP.
    echo Found: !WINESP_COUNT!
    exit /b 12
)
if /I "!SOURCE_DRIVE!"=="!WINDOWS_DRIVE!" (
    echo ERROR: installer source and Windows target resolve to the same volume.
    exit /b 13
)
if /I "!SOURCE_DRIVE!"=="!WINESP_DRIVE!" (
    echo ERROR: installer source and WINESP target resolve to the same volume.
    exit /b 14
)
if /I "!WINDOWS_DRIVE!"=="!WINESP_DRIVE!" (
    echo ERROR: Windows and WINESP resolve to the same volume.
    exit /b 15
)

set "WIM_FILE=!SOURCE_DRIVE!\sources\install.wim"
set "LOG_FILE=!SOURCE_DRIVE!\windows-reinstall.log"

echo.
echo Installer source : !SOURCE_DRIVE!  ^(!WIM_FILE!^)
echo Windows target   : !WINDOWS_DRIVE!  ^(exact label Windows^)
echo EFI target       : !WINESP_DRIVE!  ^(exact label WINESP^)
echo.
vol !SOURCE_DRIVE!
dir !WIM_FILE!
vol !WINDOWS_DRIVE!
dir !WINDOWS_DRIVE!\
vol !WINESP_DRIVE!
dir !WINESP_DRIVE!\

echo.
echo Available Windows images:
dism /Get-WimInfo /WimFile:"!WIM_FILE!"
if errorlevel 1 (
    echo ERROR: DISM could not read the installer image.
    exit /b 20
)

echo.
set "IMAGE_INDEX="
set /p "IMAGE_INDEX=Enter the image index to install: "
if not defined IMAGE_INDEX (
    echo ERROR: no image index was entered.
    exit /b 21
)
echo(!IMAGE_INDEX!| findstr /R /X "[1-9][0-9]*" >nul
if errorlevel 1 (
    echo ERROR: the image index must be a positive integer.
    exit /b 22
)

rem Validate the requested index while the operation is still non-destructive.
dism /Get-WimInfo /WimFile:"!WIM_FILE!" /Index:!IMAGE_INDEX!
if errorlevel 1 (
    echo ERROR: image index !IMAGE_INDEX! is not valid for this WIM.
    exit /b 23
)

echo.
echo WARNING: all files on !WINDOWS_DRIVE! ^(Windows^) and !WINESP_DRIVE! ^(WINESP^) will be erased.
echo Apple, Asahi, recovery, and all other partitions will remain untouched.
set "CONFIRM="
set /p "CONFIRM=Type ERASE WINDOWS exactly to continue: "
if not "!CONFIRM!"=="ERASE WINDOWS" (
    echo Cancelled. No destructive command was run.
    exit /b 24
)

rem Task 1 deliberately stops at the destructive boundary. The deployment
rem implementation is added only after its command-order tests exist.
echo ERROR: deployment stage is not implemented in this revision.
exit /b 25

:write_diskpart_scripts
>"%TEMP%\reinstall-windows-os.txt" echo select volume !WINDOWS_DRIVE:~0,1!
>>"%TEMP%\reinstall-windows-os.txt" echo format fs=ntfs quick label=Windows override
>"%TEMP%\reinstall-windows-esp.txt" echo select volume !WINESP_DRIVE:~0,1!
>>"%TEMP%\reinstall-windows-esp.txt" echo format fs=fat32 quick label=WINESP override
exit /b 0

:verify_artifacts
if not exist "!WINDOWS_DRIVE!\Windows\System32\winload.efi" exit /b 1
if not exist "!WINESP_DRIVE!\EFI\Microsoft\Boot\BCD" exit /b 1
if not exist "!WINESP_DRIVE!\EFI\BOOT\BOOTAA64.EFI" exit /b 1
exit /b 0
