# User-level UMD trace for processes a launcher starts (Steam, 3DMark): set, restart Steam, start AppId.
# Remove with trace-env-off.ps1 after the run (EXP1876G).
param([Parameter(Mandatory=$true)][string]$TraceFile,[string]$SteamApp='',[string]$ShaderDumpDir='')
[Environment]::SetEnvironmentVariable('APPLE_AGX_UMD_TRACE_FILE',$TraceFile,'User')
$env:APPLE_AGX_UMD_TRACE_FILE=$TraceFile
if($ShaderDumpDir){ New-Item -ItemType Directory -Force $ShaderDumpDir | Out-Null; [Environment]::SetEnvironmentVariable('APPLE_AGX_D3D12_SHADER_DUMP_DIR',$ShaderDumpDir,'User'); $env:APPLE_AGX_D3D12_SHADER_DUMP_DIR=$ShaderDumpDir }
Start-Process 'C:\Program Files (x86)\Steam\steam.exe' -ArgumentList '-shutdown'
Start-Sleep 25
Get-Process steam,steamwebhelper -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 3
Start-Process 'C:\Program Files (x86)\Steam\steam.exe' -ArgumentList '-silent'
if($SteamApp){ Start-Sleep 40; Start-Process 'C:\Program Files (x86)\Steam\steam.exe' -ArgumentList "-applaunch $SteamApp" }
