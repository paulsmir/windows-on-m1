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
    set "VOLUME_LABEL="
    for /f "tokens=1-5,*" %%A in ('vol %%L: 2^>nul') do (
        if /i "%%A %%B %%C %%D %%E"=="Volume in drive %%L is" set "VOLUME_LABEL=%%F"
    )
    if /i "!VOLUME_LABEL!"=="Windows" (
        set /a WINDOWS_COUNT+=1
        set "WINDOWS_DRIVE=%%L:"
    )
    if /i "!VOLUME_LABEL!"=="WINESP" (
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
set "DISM_LOG=!SOURCE_DRIVE!\windows-reinstall-dism.log"

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
set "INDEX_REMAINDER=!IMAGE_INDEX!"
for %%D in (0 1 2 3 4 5 6 7 8 9) do set "INDEX_REMAINDER=!INDEX_REMAINDER:%%D=!"
if defined INDEX_REMAINDER (
    echo ERROR: the image index must be a positive integer.
    exit /b 22
)
if "!IMAGE_INDEX!"=="0" (
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

set "OS_DISKPART=%TEMP%\reinstall-windows-os.txt"
set "ESP_DISKPART=%TEMP%\reinstall-windows-esp.txt"

>"!LOG_FILE!" echo Guarded Windows reinstall
call :log "Source: !SOURCE_DRIVE!"
call :log "Windows target: !WINDOWS_DRIVE!"
call :log "WINESP target: !WINESP_DRIVE!"
call :log "Image index: !IMAGE_INDEX!"
call :log "Confirmation accepted"

rem These files are created only after the literal confirmation succeeds.
>"!OS_DISKPART!" echo select volume !WINDOWS_DRIVE:~0,1!
>>"!OS_DISKPART!" echo format fs=ntfs quick label=Windows override
>"!ESP_DISKPART!" echo select volume !WINESP_DRIVE:~0,1!
>>"!ESP_DISKPART!" echo format fs=fat32 quick label=WINESP override

echo.
echo PHASE: format Windows volume
call :log "PHASE: format Windows volume"
diskpart /s "!OS_DISKPART!"
if errorlevel 1 (
    set "FAIL_CODE=!ERRORLEVEL!"
    set "FAIL_PHASE=format Windows volume"
    goto :failed
)
call :log "Exit code: 0"

echo.
echo PHASE: format WINESP volume
call :log "PHASE: format WINESP volume"
diskpart /s "!ESP_DISKPART!"
if errorlevel 1 (
    set "FAIL_CODE=!ERRORLEVEL!"
    set "FAIL_PHASE=format WINESP volume"
    goto :failed
)
call :log "Exit code: 0"

if not exist "X:\Scratch\" mkdir "X:\Scratch"
if errorlevel 1 (
    set "FAIL_CODE=!ERRORLEVEL!"
    set "FAIL_PHASE=create DISM scratch directory"
    goto :failed
)

echo.
echo PHASE: apply Windows image
call :log "PHASE: apply Windows image"
dism /Apply-Image /ImageFile:"!WIM_FILE!" /Index:!IMAGE_INDEX! /ApplyDir:!WINDOWS_DRIVE!\ /ScratchDir:"X:\Scratch" /LogPath:"!DISM_LOG!"
if errorlevel 1 (
    set "FAIL_CODE=!ERRORLEVEL!"
    set "FAIL_PHASE=apply Windows image"
    goto :failed
)
call :log "Exit code: 0"

echo.
echo PHASE: create Microsoft UEFI boot files
call :log "PHASE: create Microsoft UEFI boot files"
bcdboot "!WINDOWS_DRIVE!\Windows" /s !WINESP_DRIVE! /f UEFI /v
if errorlevel 1 (
    set "FAIL_CODE=!ERRORLEVEL!"
    set "FAIL_PHASE=create Microsoft UEFI boot files"
    goto :failed
)
call :log "Exit code: 0"

if not exist "!WINESP_DRIVE!\EFI\BOOT\" mkdir "!WINESP_DRIVE!\EFI\BOOT"
if errorlevel 1 (
    set "FAIL_CODE=!ERRORLEVEL!"
    set "FAIL_PHASE=create fallback boot directory"
    goto :failed
)

echo.
echo PHASE: create ARM64 fallback loader
call :log "PHASE: create ARM64 fallback loader"
copy /y "!WINESP_DRIVE!\EFI\Microsoft\Boot\bootmgfw.efi" "!WINESP_DRIVE!\EFI\BOOT\BOOTAA64.EFI"
if errorlevel 1 (
    set "FAIL_CODE=!ERRORLEVEL!"
    set "FAIL_PHASE=create ARM64 fallback loader"
    goto :failed
)
call :log "Exit code: 0"

goto :verify_artifacts

:verify_artifacts
echo.
echo PHASE: verify installed boot artifacts
call :log "PHASE: verify installed boot artifacts"
if not exist "!WINDOWS_DRIVE!\Windows\System32\winload.efi" (
    set "FAIL_CODE=31"
    set "FAIL_PHASE=verify winload.efi"
    goto :failed
)
if not exist "!WINESP_DRIVE!\EFI\Microsoft\Boot\BCD" (
    set "FAIL_CODE=32"
    set "FAIL_PHASE=verify BCD"
    goto :failed
)
if not exist "!WINESP_DRIVE!\EFI\BOOT\BOOTAA64.EFI" (
    set "FAIL_CODE=33"
    set "FAIL_PHASE=verify BOOTAA64.EFI"
    goto :failed
)
call :log "Exit code: 0"
call :log "RESULT: SUCCESS"
echo.
echo ============================================================
echo RESULT: SUCCESS
echo Windows was redeployed and ARM64 UEFI boot files were rebuilt.
echo Log: !LOG_FILE!
echo Remove the installer USB before booting Windows.
echo ============================================================
del /q "!OS_DISKPART!" "!ESP_DISKPART!" >nul 2>&1
exit /b 0

:failed
if not defined FAIL_CODE set "FAIL_CODE=1"
if not defined FAIL_PHASE set "FAIL_PHASE=unknown"
call :log "Exit code: !FAIL_CODE!"
call :log "RESULT: FAILED - !FAIL_PHASE!"
echo.
echo RESULT: FAILED
echo Phase: !FAIL_PHASE!
echo Exit code: !FAIL_CODE!
echo Main log: !LOG_FILE!
echo DISM log: !DISM_LOG!
del /q "!OS_DISKPART!" "!ESP_DISKPART!" >nul 2>&1
exit /b !FAIL_CODE!

:log
>>"!LOG_FILE!" echo %~1
exit /b 0
