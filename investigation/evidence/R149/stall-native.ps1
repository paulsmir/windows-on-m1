$ErrorActionPreference='Stop'
Set-Location C:\agx\r149-stall
cmd /c '"C:\VS2022Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cl /nologo /EHsc readetl.cpp /link advapi32.lib /out:readetl.exe' *> build.txt
if($LASTEXITCODE -ne 0){throw 'build failed'}
.\readetl.exe EXP862.etl | Out-File -Encoding ascii native-summary.txt
