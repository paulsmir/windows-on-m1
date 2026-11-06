@echo off
rem ANS006: read-only mount of the native AppleAns disk and sequential read throughput.
rem Safety: the disk read-only attribute is set and VERIFIED before any online; if it cannot be
rem proven the disk is never onlined. The driver (read-only V1) and the EL2 broker also reject writes.
setlocal EnableExtensions EnableDelayedExpansion
set R=%OUT%\ANS006
mkdir "%R%" 2>nul
>"%R%\STATUS.TXT" echo stage=start
set NDISK=
for %%N in (0 1 2 3 4 5 6 7) do call :probe %%N
>>"%R%\STATUS.TXT" echo native_disk=!NDISK!
if not defined NDISK goto done
>"%R%\RO.TXT" echo select disk !NDISK!
>>"%R%\RO.TXT" echo attributes disk set readonly
>>"%R%\RO.TXT" echo detail disk
>>"%R%\RO.TXT" echo exit
diskpart /s "%R%\RO.TXT" >"%R%\RO.LOG" 2>&1
set ROK=0
for /f "usebackq tokens=1,2,3,4" %%A in ("%R%\RO.LOG") do if "%%A %%B %%C"=="Read-only : Yes" set ROK=1
for /f "usebackq tokens=1,2,3,4,5" %%A in ("%R%\RO.LOG") do if "%%A %%B %%C %%D %%E"=="Current Read-only State : Yes" set ROK=1
>>"%R%\STATUS.TXT" echo readonly_verified=!ROK!
if not "!ROK!"=="1" goto done
>"%R%\SAN.TXT" echo san policy=OnlineAll
>>"%R%\SAN.TXT" echo exit
diskpart /s "%R%\SAN.TXT" >"%R%\SAN.LOG" 2>&1
set ONL=0
for %%T in (1 2 3 4 5) do if "!ONL!"=="0" call :online %%T
>"%R%\SANR.TXT" echo san policy=OfflineAll
>>"%R%\SANR.TXT" echo exit
diskpart /s "%R%\SANR.TXT" >"%R%\SANR.LOG" 2>&1
>>"%R%\STATUS.TXT" echo online_ok=!ONL!
if not "!ONL!"=="1" goto done
ping -n 6 127.0.0.1 >nul
>"%R%\VOL.TXT" echo select disk !NDISK!
>>"%R%\VOL.TXT" echo detail disk
>>"%R%\VOL.TXT" echo list volume
>>"%R%\VOL.TXT" echo exit
diskpart /s "%R%\VOL.TXT" >"%R%\VOL.LOG" 2>&1
mountvol >"%R%\MOUNTVOL.TXT" 2>&1
set BEST=
set BESTSIZE=0
for %%L in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do call :scan %%L
>>"%R%\STATUS.TXT" echo file=!BEST! bytes=!BESTSIZE!
if not defined BEST goto offline
call :typefile "!BEST!" 1
call :typefile "!BEST!" 2
for %%F in (!WINVOL!:\Windows\System32\config\SOFTWARE !WINVOL!:\Windows\System32\config\SYSTEM !WINVOL!:\Windows\System32\config\COMPONENTS !WINVOL!:\Windows\System32\config\DRIVERS !WINVOL!:\Windows\System32\MRT.exe) do if exist "%%F" call :copyfile "%%F" %%~zF
:offline
wevtutil epl System "%R%\System.evtx" >"%R%\WEVT.LOG" 2>&1
>"%R%\OFF.TXT" echo select disk !NDISK!
>>"%R%\OFF.TXT" echo offline disk
>>"%R%\OFF.TXT" echo exit
diskpart /s "%R%\OFF.TXT" >"%R%\OFF.LOG" 2>&1
>>"%R%\STATUS.TXT" echo offline_exit=!errorlevel!
:done
>>"%R%\STATUS.TXT" echo stage=done
endlocal
exit /b 0

:probe
if defined NDISK exit /b 0
>"%R%\P%1.TXT" echo select disk %1
>>"%R%\P%1.TXT" echo detail disk
>>"%R%\P%1.TXT" echo exit
diskpart /s "%R%\P%1.TXT" >"%R%\P%1.LOG" 2>&1
for /f "usebackq delims=" %%A in ("%R%\P%1.LOG") do (set LN=%%A& if not "!LN:APPLE SSD=!"=="!LN!" set NDISK=%1)
exit /b 0

:online
>"%R%\ON%1A.TXT" echo rescan
>>"%R%\ON%1A.TXT" echo list disk
>>"%R%\ON%1A.TXT" echo exit
diskpart /s "%R%\ON%1A.TXT" >"%R%\ON%1A.LOG" 2>&1
ping -n 6 127.0.0.1 >nul
>"%R%\ON%1B.TXT" echo list disk
>>"%R%\ON%1B.TXT" echo select disk !NDISK!
>>"%R%\ON%1B.TXT" echo online disk
>>"%R%\ON%1B.TXT" echo exit
diskpart /s "%R%\ON%1B.TXT" >"%R%\ON%1B.LOG" 2>&1
set RC=!errorlevel!
>>"%R%\STATUS.TXT" echo online_try=%1 rc=!RC!
if "!RC!"=="0" set ONL=1
exit /b 0

:scan
if /i "%1"=="X" exit /b 0
if not exist %1:\Windows\System32\ntoskrnl.exe exit /b 0
>>"%R%\STATUS.TXT" echo windows_volume=%1
set WINVOL=%1
for %%F in (%1:\pagefile.sys %1:\hiberfil.sys %1:\swapfile.sys %1:\Windows\MEMORY.DMP) do if exist "%%F" call :consider "%%F" %%~zF
exit /b 0

:consider
rem cmd arithmetic is 32-bit: compare sizes as zero-padded strings.
set S=0000000000000%2
set S=!S:~-13!
set B=0000000000000!BESTSIZE!
set B=!B:~-13!
if "!S!" GTR "!B!" (set BEST=%~1& set BESTSIZE=%2)
exit /b 0

:typefile
set T0=%time%
type "%~1" >NUL 2>"%R%\TYPE%2.ERR"
set CRC=!errorlevel!
set T1=%time%
>>"%R%\STATUS.TXT" echo type%2 file=%~1 bytes=!BESTSIZE! rc=!CRC! t0=!T0! t1=!T1!
exit /b 0
:copyfile
set T0=%time%
copy /b "%~1" NUL >"%R%\COPYF.LOG" 2>&1
set CRC=!errorlevel!
set T1=%time%
>>"%R%\STATUS.TXT" echo copy file=%~1 bytes=%2 rc=!CRC! t0=!T0! t1=!T1!
exit /b 0
:readfile
set T0=%time%
copy /b "%~1" NUL >"%R%\COPY%2.LOG" 2>&1
set CRC=!errorlevel!
set T1=%time%
>>"%R%\STATUS.TXT" echo read%2 file=%~1 bytes=!BESTSIZE! rc=!CRC! t0=!T0! t1=!T1!
exit /b 0
