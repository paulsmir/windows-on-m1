@echo off
setlocal EnableExtensions EnableDelayedExpansion
if /I not "%SystemDrive%"=="X:" exit /b 91
set RAM=X:\EXP931
md "%RAM%"
set STEP=volume-identities
echo EXP931 offline NTFS repair - verifying exact volume identities
X:\Windows\System32\RecoveryVolumeIdentity.exe >"%RAM%\identities.txt" 2>&1
if errorlevel 1 goto fail
mountvol T:\ /L >"%RAM%\target-guid.txt" 2>&1
set STEP=ntfs-info
fsutil fsinfo ntfsinfo T: >"%RAM%\ntfs-before.txt" 2>&1
if errorlevel 1 goto fail
rem Exact serial/GPT/geometry were checked through FSCTL by the native helper.
set STEP=system-hive-exists
if not exist T:\Windows\System32\config\SYSTEM goto fail
set STEP=winload-exists
if not exist T:\Windows\System32\winload.efi goto fail
mountvol R:\ /L >"%RAM%\evidence-guid.txt" 2>&1
set OUT=R:\J313-EXP931D-recovery
set STEP=fresh-evidence-directory
if exist "%OUT%" goto fail
md "%OUT%" || goto fail
>"%OUT%\manifest.txt" echo Experiment=EXP931D
>>"%OUT%\manifest.txt" echo TargetGuid=54ba924f-c5e4-4898-8192-327d5cad900e
>>"%OUT%\manifest.txt" echo TargetSerial=6612cadc12caaffb
>>"%OUT%\manifest.txt" echo Target=T:
>>"%OUT%\manifest.txt" echo RunningSystem=%SystemDrive%
>>"%OUT%\manifest.txt" echo Started=%DATE% %TIME%
copy /y "%RAM%\*.txt" "%OUT%\" >nul
rem Back up the offline base hive as well as the already host-verified live hive.
set STEP=offline-hive-copy
copy /b T:\Windows\System32\config\SYSTEM "%OUT%\SYSTEM-before.hiv" >"%OUT%\hive-copy.txt" 2>&1
if errorlevel 1 goto fail
set STEP=offline-hive-byte-compare
X:\Windows\System32\RecoveryVolumeIdentity.exe --compare-files T:\Windows\System32\config\SYSTEM "%OUT%\SYSTEM-before.hiv" >"%OUT%\hive-compare.txt" 2>&1
if errorlevel 1 goto fail
fsutil dirty query T: >"%OUT%\dirty-before.txt" 2>&1
echo EXP931 identity and backup gates passed. CHKDSK now repairs only T:.
chkdsk.exe T: /f /x >"%OUT%\chkdsk.txt" 2>&1
set RC=!ERRORLEVEL!
>>"%OUT%\manifest.txt" echo ChkdskExit=!RC!
>>"%OUT%\manifest.txt" echo Finished=%DATE% %TIME%
type "%OUT%\chkdsk.txt"
fsutil dirty query T: >"%OUT%\dirty-after.txt" 2>&1
fsutil fsinfo ntfsinfo T: >"%OUT%\ntfs-after.txt" 2>&1
>>"%OUT%\manifest.txt" echo Complete=1
if !RC! GTR 1 >>"%OUT%\manifest.txt" echo Verdict=REPAIR_NOT_CONFIRMED
if !RC! LEQ 1 >>"%OUT%\manifest.txt" echo Verdict=CHECK_WINDOWS_REGISTRY_FLUSH_NEXT
rem Return through the normal GPU-visible, broker-disabled recovery profile.
wpeutil.exe reboot
exit /b !RC!
:fail
echo EXP931 IDENTITY OR BACKUP GATE FAILED. No further repair action.
if defined OUT if exist "%OUT%" >>"%OUT%\manifest.txt" echo GateFailure=1
type "%RAM%\*.txt"
if defined OUT if exist "%OUT%\hive-copy.txt" type "%OUT%\hive-copy.txt"
if defined OUT if exist "%OUT%\hive-compare.txt" type "%OUT%\hive-compare.txt"
echo FAILED_STEP=!STEP!
pause
exit /b 91
