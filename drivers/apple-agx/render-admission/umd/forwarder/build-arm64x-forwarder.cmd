@echo off
rem usage: build-arm64x-forwarder.cmd <out directory> <package build>
call "C:\VS2022Community\Common7\Tools\VsDevCmd.bat" -arch=arm64 -host_arch=x64 >nul 2>&1
if errorlevel 1 exit /b 2
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-arm64x-forwarder.ps1" -Out %1 -PackageBuild %2
exit /b %ERRORLEVEL%
