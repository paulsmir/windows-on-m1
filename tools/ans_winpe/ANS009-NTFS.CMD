@echo off
rem ANS009: first native NTFS writes, confined to one dedicated 1 GiB volume labelled ANSTEST.
rem Safety layers: (1) EL2 admits WRITE only inside the sealed LBA window = exactly that partition, so
rem writes to C:, the ESPs or macOS are DENIED before any doorbell; (2) the disk is onlined only after the
rem read-only attribute is VERIFIED; it is cleared only for the write/verify phase and set again after it.
setlocal EnableExtensions EnableDelayedExpansion
set R=%OUT%\ANS009
set T=T
mkdir "%R%" 2>nul
rem Live evidence on the stick: survives a hang (the normal export runs only at the end).
set LIVE=
if defined LETTER set LIVE=!LETTER!:\ANS001-RESULTS-27a6a231\ANS009-LIVE-%RANDOM%
if defined LIVE mkdir "!LIVE!" 2>nul
>"%R%\STATUS.TXT" echo stage=start
set NDISK=
for %%N in (0 1 2 3 4 5 6 7) do call :probe %%N
>>"%R%\STATUS.TXT" echo native_disk=!NDISK!
call :live
if not defined NDISK goto done
call :attr set RO1
>>"%R%\STATUS.TXT" echo readonly_verified=!ROK!
call :live
if not "!ROK!"=="1" goto done
>"%R%\SAN.TXT" echo san policy=OnlineAll
>>"%R%\SAN.TXT" echo exit
diskpart /s "%R%\SAN.TXT" >"%R%\SAN.LOG" 2>&1
set ONL=0
for %%N in (1 2 3 4 5) do if "!ONL!"=="0" call :online %%N
>"%R%\SANR.TXT" echo san policy=OfflineAll
>>"%R%\SANR.TXT" echo exit
diskpart /s "%R%\SANR.TXT" >"%R%\SANR.LOG" 2>&1
>>"%R%\STATUS.TXT" echo online_ok=!ONL!
call :live
if not "!ONL!"=="1" goto done
ping -n 6 127.0.0.1 >nul
rem Find the ANSTEST volume on the native disk and give it letter T.
>"%R%\LV.TXT" echo list volume
>>"%R%\LV.TXT" echo exit
diskpart /s "%R%\LV.TXT" >"%R%\LV.LOG" 2>&1
set VOL=
for /f "usebackq tokens=1,2,3,4" %%A in ("%R%\LV.LOG") do if /i "%%A"=="Volume" if /i "%%D"=="ANSTEST" set VOL=%%B
for /f "usebackq tokens=1,2,3" %%A in ("%R%\LV.LOG") do if /i "%%A"=="Volume" if /i "%%C"=="ANSTEST" set VOL=%%B
>>"%R%\STATUS.TXT" echo anstest_volume=!VOL!
call :live
if not defined VOL goto protect
>"%R%\AS.TXT" echo select volume !VOL!
>>"%R%\AS.TXT" echo assign letter=%T%
>>"%R%\AS.TXT" echo detail volume
>>"%R%\AS.TXT" echo exit
diskpart /s "%R%\AS.TXT" >"%R%\AS.LOG" 2>&1
>>"%R%\STATUS.TXT" echo assign_rc=!errorlevel!
call :live
fsutil fsinfo volumeinfo %T%: >"%R%\VOLINFO1.LOG" 2>&1
set ISTEST=0
for /f "usebackq tokens=1,2,3,4,5" %%A in ("%R%\VOLINFO1.LOG") do if /i "%%A %%B %%C %%D"=="Volume Name : ANSTEST" set ISTEST=1
if /i "!LETTER!"=="%T%" set ISTEST=0
>>"%R%\STATUS.TXT" echo t_is_anstest=!ISTEST! stick=!LETTER!
call :live
if not "!ISTEST!"=="1" goto protect
rem ANS009-R3: a writable C: whose writes EL2 denies wedged the disk flush (diskpart set readonly hung).
rem Take every other lettered volume on the native disk offline while the disk is still read-only.
>"%R%\DD.TXT" echo select disk !NDISK!
>>"%R%\DD.TXT" echo detail disk
>>"%R%\DD.TXT" echo exit
diskpart /s "%R%\DD.TXT" >"%R%\DD.LOG" 2>&1
set OFFV=
for /f "usebackq tokens=1,2,3" %%A in ("%R%\DD.LOG") do if /i "%%A"=="Volume" call :offvol "%%C"
>>"%R%\STATUS.TXT" echo other_volumes_offlined=!OFFV!
call :live
mountvol >"%R%\MOUNTVOL-AFTER.TXT" 2>&1
call :attr clear RW
>>"%R%\STATUS.TXT" echo readonly_cleared=!RWOK!
call :live
if not "!RWOK!"=="1" goto protect
rem Drop the read-only mount so the next access mounts NTFS writable.
fsutil volume dismount %T%: >"%R%\DISMOUNT0.LOG" 2>&1
>>"%R%\STATUS.TXT" echo dismount0_rc=!errorlevel!
call :live
fsutil fsinfo volumeinfo %T%: >"%R%\VOLINFO2.LOG" 2>&1
set T0=%time%
set RLOG=%R%\COPY.LOG
if defined LIVE set RLOG=!LIVE!\ROBOCOPY.LOG
robocopy X:\Windows\System32 %T%:\ANS009\SYS32 *.dll /R:0 /W:0 /NP /NDL /TS /BYTES /LOG:"!RLOG!" >nul 2>&1
set CRC=!errorlevel!
set T1=%time%
>>"%R%\STATUS.TXT" echo copy_rc=!CRC! t0=!T0! t1=!T1!
call :live
dir /s /-c %T%:\ANS009 >"%R%\DIR.LOG" 2>&1
rem Flush + dismount: the compare below must read the media, not the cache.
fsutil volume dismount %T%: >"%R%\DISMOUNT1.LOG" 2>&1
>>"%R%\STATUS.TXT" echo dismount1_rc=!errorlevel!
call :live
rem Full content verification is done by recovery over the bridge (hashes); fc /b over 552 MB is a
rem CPU-bound consumer that overran the 1500 s budget in ANS009-R1. Compare the first and last files only.
set BAD=0
set N=0
set FIRST=
set LAST=
for %%F in (X:\Windows\System32\*.dll) do (
 if not defined FIRST set "FIRST=%%~nxF"
 set "LAST=%%~nxF"
)
call :cmp "!FIRST!"
call :cmp "!LAST!"
fsutil volume dismount %T%: >"%R%\DISMOUNT2.LOG" 2>&1
>>"%R%\STATUS.TXT" echo dismount2_rc=!errorlevel! first=!FIRST! last=!LAST!
call :live
>>"%R%\STATUS.TXT" echo compared=!N! mismatches=!BAD!
call :live
chkdsk %T%: >"%R%\CHKDSK.LOG" 2>&1
>>"%R%\STATUS.TXT" echo chkdsk_rc=!errorlevel!
call :live
:protect
call :attr set RO2
>>"%R%\STATUS.TXT" echo readonly_restored=!ROK!
call :live
wevtutil epl System "%R%\System.evtx" >"%R%\WEVT.LOG" 2>&1
>"%R%\OFF.TXT" echo select disk !NDISK!
>>"%R%\OFF.TXT" echo offline disk
>>"%R%\OFF.TXT" echo exit
diskpart /s "%R%\OFF.TXT" >"%R%\OFF.LOG" 2>&1
>>"%R%\STATUS.TXT" echo offline_exit=!errorlevel!
call :live
:done
>>"%R%\STATUS.TXT" echo stage=done
call :live
endlocal
exit /b 0

:offvol
set L=%~1
if not "!L:~1!"=="" exit /b 0
if /i "!L!"=="%T%" exit /b 0
if /i "!L!"=="!LETTER!" exit /b 0
if /i "!L!"=="X" exit /b 0
mountvol !L!: /P >>"%R%\OFFVOL.LOG" 2>&1
set OFFV=!OFFV!!L!(!errorlevel!)
exit /b 0

:live
if defined LIVE copy /y "%R%\STATUS.TXT" "!LIVE!\STATUS.TXT" >nul 2>&1
exit /b 0

:cmp
set /a N+=1
fc /b "X:\Windows\System32\%~1" "%T%:\ANS009\SYS32\%~1" >>"%R%\FC.LOG" 2>&1
if errorlevel 1 (set /a BAD+=1& >>"%R%\MISMATCH.LOG" echo %~1)
exit /b 0

:attr
>"%R%\%2.TXT" echo select disk !NDISK!
>>"%R%\%2.TXT" echo attributes disk %1 readonly
>>"%R%\%2.TXT" echo detail disk
>>"%R%\%2.TXT" echo exit
diskpart /s "%R%\%2.TXT" >"%R%\%2.LOG" 2>&1
set ROK=0
set RWOK=0
for /f "usebackq tokens=1,2,3,4" %%A in ("%R%\%2.LOG") do if "%%A %%B %%C"=="Read-only : Yes" set ROK=1
for /f "usebackq tokens=1,2,3,4,5" %%A in ("%R%\%2.LOG") do if "%%A %%B %%C %%D %%E"=="Current Read-only State : Yes" set ROK=1
for /f "usebackq tokens=1,2,3,4" %%A in ("%R%\%2.LOG") do if "%%A %%B %%C"=="Read-only : No" set RWOK=1
for /f "usebackq tokens=1,2,3,4,5" %%A in ("%R%\%2.LOG") do if "%%A %%B %%C %%D %%E"=="Current Read-only State : Yes" set RWOK=0
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
call :live
if "!RC!"=="0" set ONL=1
exit /b 0
