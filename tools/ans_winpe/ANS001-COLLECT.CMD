@echo off
setlocal EnableExtensions EnableDelayedExpansion
if /i not "%SystemDrive%"=="X:" goto refused
if /i not "%SystemRoot%"=="X:\Windows" goto refused
reg query HKLM\SYSTEM\CurrentControlSet\Control\MiniNT >nul 2>&1
if errorlevel 1 goto refused
set OUT=X:\ANS001-RESULTS
set TEMP=X:\Windows\Temp
set TMP=X:\Windows\Temp
mkdir "%OUT%" 2>nul
mkdir "%OUT%\Scratch" 2>nul
>"%OUT%\COMMANDS.CSV" echo check,exit_code
>"%OUT%\IDENTITY.TXT" echo ANS001 enumeration only - logs live exclusively in WinPE RAM
>>"%OUT%\IDENTITY.TXT" ver
>>"%OUT%\IDENTITY.TXT" echo DATE=%DATE% TIME=%TIME% SYSTEMDRIVE=%SystemDrive% SYSTEMROOT=%SystemRoot%
rem SAN policy must already have been set offline, before this guest boot.
reg query HKLM\SYSTEM\CurrentControlSet\Services\partmgr\Parameters /v SanPolicy >"%OUT%\SAN-POLICY.TXT" 2>&1
set SANQUERYRC=!errorlevel!
set SANMATCH=0
for /f "usebackq tokens=1,2,3,*" %%A in ("%OUT%\SAN-POLICY.TXT") do if /i "%%A"=="SanPolicy" if /i "%%B"=="REG_DWORD" if /i "%%C"=="0x3" if "%%D"=="" set /a SANMATCH+=1
if not "!SANQUERYRC!:!SANMATCH!"=="0:1" (
  >"%OUT%\INCOMPLETE.TXT" echo REFUSED: preconfigured offline-all SAN policy not proved; wpeinit not called
  goto finished
)
wpeinit >"%OUT%\WPEINIT-CONSOLE.TXT" 2>&1
>>"%OUT%\COMMANDS.CSV" echo wpeinit,!errorlevel!
call :capture ans pnputil /enum-devices /deviceid "ACPI\APPL0003" /deviceids /drivers /properties
call :capture connected pnputil /enum-devices /connected /deviceids /drivers /properties
call :capture problems pnputil /enum-devices /problem /deviceids /drivers
call :capture packages pnputil /enum-drivers /files
call :capture ans-reg reg query HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0003 /s
rem sc.exe is absent from both audited WIM indices. Do not invent active service state.
>"%OUT%\OPTIONAL-UNAVAILABLE.TXT" echo sc.exe: active driver service query unavailable; registry-services captures configuration only, loaded captures driverquery.
>>"%OUT%\COMMANDS.CSV" echo services-sc-optional-unavailable,127
call :capture loaded driverquery /v /fo csv
call :capture disks pnputil /enum-devices /class DiskDrive /connected /deviceids /drivers
call :capture volumes mountvol
call :capture disk-wmi X:\Windows\System32\wbem\wmic.exe diskdrive get "Index,Model,PNPDeviceID,Size,Status" /format:list
rem ANSCOLLECT003 observed DISM /online Get-Drivers 0x80004002 in this WinPE.
rem Retain the actual exit/output as optional; pnputil packages and offline structural audit remain package evidence.
>"%OUT%\OPTIONAL-CHECKS.CSV" echo check,classification,reason
>>"%OUT%\OPTIONAL-CHECKS.CSV" echo services-sc-optional-unavailable,optional-unavailable,sc.exe absent from both audited indices
>>"%OUT%\OPTIONAL-CHECKS.CSV" echo dism-drivers,optional-unsupported,ANSCOLLECT003 observed 0x80004002 No such interface supported
call :capture dism-drivers dism /online /Get-Drivers /All /Format:List /ScratchDir:X:\ANS001-RESULTS\Scratch /LogPath:X:\ANS001-RESULTS\DISM.LOG
call :capture registry-services reg query HKLM\SYSTEM\CurrentControlSet\Services /s
call :capture pnp-all-reg reg query HKLM\SYSTEM\CurrentControlSet\Enum\PCI /s
if exist X:\Windows\INF\setupapi.dev.log copy /y X:\Windows\INF\setupapi.dev.log "%OUT%\SETUPAPI.DEV.LOG" >nul
if exist X:\Windows\System32\wpeinit.log copy /y X:\Windows\System32\wpeinit.log "%OUT%\WPEINIT.LOG" >nul
>"%OUT%\COLLECTED.TXT" echo Collected - human audit required. Required command failures mean incomplete evidence, never PASS.
>>"%OUT%\COLLECTED.TXT" echo Optional checks are listed in OPTIONAL-CHECKS.CSV; their actual COMMANDS.CSV exits and output remain evidence, never substituted with success.
:finished
echo ANS001 collection stopped. Logs are in X:\ANS001-RESULTS. Do not reboot before evidence capture.
echo No log export, disk mount, driver operation, trust modification or shutdown is automatic.
exit /b 0
:refused
echo ANS001 REFUSED: not proven WinPE RAM environment.
exit /b 90
:capture
%2 %3 %4 %5 %6 %7 %8 %9 >"%OUT%\%1.TXT" 2>&1
set RC=!errorlevel!
>>"%OUT%\COMMANDS.CSV" echo %1,!RC!
exit /b 0
