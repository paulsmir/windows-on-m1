@echo off
setlocal EnableExtensions EnableDelayedExpansion
if /I not "%SystemDrive%"=="X:" exit /b 91
set RAM=X:\EXP1048
md "%RAM%"
set STEP=volume-identities
echo EXP1048 read-only inspection - verifying exact volume identities
X:\Windows\System32\RecoveryVolumeIdentity.exe >"%RAM%\identities.txt" 2>&1
if errorlevel 1 goto fail
mountvol T:\ /L >"%RAM%\target-guid.txt" 2>&1
mountvol R:\ /L >"%RAM%\evidence-guid.txt" 2>&1
set STEP=system-hive-exists
if not exist T:\Windows\System32\config\SYSTEM goto fail
set OUT=R:\J313-EXP1048-inspection
set STEP=fresh-evidence-directory
if exist "%OUT%" goto fail
md "%OUT%" || goto fail
>"%OUT%\manifest.txt" echo Experiment=EXP1048
>>"%OUT%\manifest.txt" echo Started=%DATE% %TIME%
copy /y "%RAM%\*.txt" "%OUT%\" >nul
echo ===== EXP1047 manifest =====
type R:\J313-EXP1047-recovery\manifest.txt
echo ===== EXP1047 dirty before/after =====
type R:\J313-EXP1047-recovery\dirty-before.txt
type R:\J313-EXP1047-recovery\dirty-after.txt
dir /a T:\Windows\System32\config >"%OUT%\config-dir.txt" 2>&1
dir /a T:\Windows\System32\config\RegBack >"%OUT%\regback-dir.txt" 2>&1
dir /a /o-d T:\Windows\Minidump >"%OUT%\minidump-dir.txt" 2>&1
dir /a T:\Windows\MEMORY.DMP >"%OUT%\memory-dmp-dir.txt" 2>&1
md "%OUT%\Minidump" >nul 2>&1
copy /b T:\Windows\Minidump\*.dmp "%OUT%\Minidump\" >"%OUT%\minidump-copy.txt" 2>&1
set STEP=hive-copies
md "%RAM%\hives"
set RESULT=
for %%H in (SYSTEM SOFTWARE SAM SECURITY DEFAULT) do (
  copy /b T:\Windows\System32\config\%%H "%RAM%\hives\%%H" >nul 2>&1
  copy /b T:\Windows\System32\config\%%H.LOG1 "%RAM%\hives\%%H.LOG1" >nul 2>&1
  copy /b T:\Windows\System32\config\%%H.LOG2 "%RAM%\hives\%%H.LOG2" >nul 2>&1
  reg load HKLM\EXP1048_%%H "%RAM%\hives\%%H" >"%RAM%\load-%%H.txt" 2>&1
  if errorlevel 1 (set RESULT=!RESULT! %%H=LOADFAIL) else (set RESULT=!RESULT! %%H=OK& reg unload HKLM\EXP1048_%%H >nul 2>&1)
)
>>"%OUT%\manifest.txt" echo HiveLoad=!RESULT!
copy /y "%RAM%\load-*.txt" "%OUT%\" >nul
reg load HKLM\EXP1048_SYS2 "%RAM%\hives\SYSTEM" >nul 2>&1
if not errorlevel 1 (
  reg query HKLM\EXP1048_SYS2\Select >"%OUT%\select.txt" 2>&1
  reg query HKLM\EXP1048_SYS2\ControlSet001\Services\AppleAgxRenderAdmission >"%OUT%\agx-service.txt" 2>&1
  reg unload HKLM\EXP1048_SYS2 >nul 2>&1
)
>>"%OUT%\manifest.txt" echo Finished=%DATE% %TIME%
>>"%OUT%\manifest.txt" echo Complete=1
echo ===== EXP1048 RESULT =====
echo HiveLoad=!RESULT!
type "%OUT%\load-SYSTEM.txt"
echo ----- config -----
type "%OUT%\config-dir.txt"
echo ----- minidumps -----
type "%OUT%\minidump-dir.txt"
type "%OUT%\agx-service.txt"
echo EXP1048 COMPLETE - read-only inspection, no repair performed.
pause
exit /b 0
:fail
echo EXP1048 IDENTITY GATE FAILED. No further action.
type "%RAM%\*.txt"
echo FAILED_STEP=!STEP!
pause
exit /b 91
