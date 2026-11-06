@echo off
rem ANS008-R1: first native write. Raw pattern into the GPT Microsoft Reserved partition
rem (LBA 30912256, 4096 blocks, all zero before) via ansraw.exe, read back natively.
rem Safety layers: (1) EL2 broker admits WRITE only inside the sealed LBA window = the MSR;
rem (2) ansraw refuses any range that is not exactly the MSR or not all zero before writing;
rem (3) the disk is onlined only after the read-only attribute is VERIFIED, and that attribute
rem is cleared only around the single ansraw write step and set again right after it.
setlocal EnableExtensions EnableDelayedExpansion
set R=%OUT%\ANS008
set FIRST=30912256
set COUNT=4096
mkdir "%R%" 2>nul
>"%R%\STATUS.TXT" echo stage=start
set NDISK=
for %%N in (0 1 2 3 4 5 6 7) do call :probe %%N
>>"%R%\STATUS.TXT" echo native_disk=!NDISK!
if not defined NDISK goto done
call :attr set RO1
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
X:\Windows\System32\ansraw.exe !NDISK! %FIRST% %COUNT% check >"%R%\ANSRAW-CHECK1.LOG" 2>&1
>>"%R%\STATUS.TXT" echo check1_rc=!errorlevel!
call :attr clear RW
>>"%R%\STATUS.TXT" echo readonly_cleared=!RWOK!
if not "!RWOK!"=="1" goto protect
X:\Windows\System32\ansraw.exe !NDISK! %FIRST% %COUNT% write >"%R%\ANSRAW-WRITE.LOG" 2>&1
>>"%R%\STATUS.TXT" echo write_rc=!errorlevel!
:protect
call :attr set RO2
>>"%R%\STATUS.TXT" echo readonly_restored=!ROK!
X:\Windows\System32\ansraw.exe !NDISK! %FIRST% %COUNT% check >"%R%\ANSRAW-CHECK2.LOG" 2>&1
>>"%R%\STATUS.TXT" echo check2_rc=!errorlevel!
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

:attr
rem %1 = set|clear, %2 = log tag. ROK=1 when read-only is verified Yes, RWOK=1 when verified No.
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
if "!RC!"=="0" set ONL=1
exit /b 0
