@echo off
setlocal EnableExtensions EnableDelayedExpansion
if /I not "%SystemDrive%"=="X:" exit /b 91
set RAM=X:\EXP931
md "%RAM%"
echo EXP931 offline NTFS repair - verifying exact volume identities
if exist T:\ goto fail
if exist R:\ goto fail
mountvol T: \\?\Volume{54ba924f-c5e4-4898-8192-327d5cad900e}\ >"%RAM%\mount-target.txt" 2>&1
if errorlevel 1 goto fail
mountvol T: /L >"%RAM%\target-guid.txt" 2>&1
findstr /i /c:"54ba924f-c5e4-4898-8192-327d5cad900e" "%RAM%\target-guid.txt" >nul || goto fail
fsutil fsinfo ntfsinfo T: >"%RAM%\ntfs-before.txt" 2>&1
if errorlevel 1 goto fail
findstr /i /c:"0x6612cadc12caaffb" "%RAM%\ntfs-before.txt" >nul || goto fail
if not exist T:\Windows\System32\config\SYSTEM goto fail
if not exist T:\Windows\System32\winload.efi goto fail
mountvol R: \\?\Volume{f05b9952-ee05-44e1-bcc4-b678a8501b2a}\ >"%RAM%\mount-evidence.txt" 2>&1
if errorlevel 1 goto fail
mountvol R: /L >"%RAM%\evidence-guid.txt" 2>&1
findstr /i /c:"f05b9952-ee05-44e1-bcc4-b678a8501b2a" "%RAM%\evidence-guid.txt" >nul || goto fail
set OUT=R:\J313-EXP931-recovery
if exist "%OUT%" goto fail
md "%OUT%" || goto fail
>"%OUT%\manifest.txt" echo Experiment=EXP931
>>"%OUT%\manifest.txt" echo TargetGuid=54ba924f-c5e4-4898-8192-327d5cad900e
>>"%OUT%\manifest.txt" echo TargetSerial=6612cadc12caaffb
>>"%OUT%\manifest.txt" echo Target=T:
>>"%OUT%\manifest.txt" echo RunningSystem=%SystemDrive%
>>"%OUT%\manifest.txt" echo Started=%DATE% %TIME%
copy /y "%RAM%\*.txt" "%OUT%\" >nul
rem Back up the offline base hive as well as the already host-verified live hive.
copy /b T:\Windows\System32\config\SYSTEM "%OUT%\SYSTEM-before.hiv" >"%OUT%\hive-copy.txt" 2>&1
if errorlevel 1 goto fail
fc /b T:\Windows\System32\config\SYSTEM "%OUT%\SYSTEM-before.hiv" >"%OUT%\hive-compare.txt" 2>&1
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
pause
exit /b 91
