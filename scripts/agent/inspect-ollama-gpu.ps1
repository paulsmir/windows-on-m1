$ErrorActionPreference = 'Stop'
Get-CimInstance Win32_Process | Where-Object { $_.Name -like '*ollama*' } | Select-Object ProcessId,ExecutablePath,CommandLine | ConvertTo-Json -Depth 3
Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion | ConvertTo-Json
Get-Command ollama | Select-Object Source | ConvertTo-Json
Get-NetTCPConnection -State Listen | Where-Object LocalPort -eq 11434 | Select-Object LocalAddress,OwningProcess | ConvertTo-Json
Get-ChildItem Env: | Where-Object { $_.Name -match 'OLLAMA|HIP|ROCR|CUDA|VULKAN|GGML' } | Sort-Object Name | Select-Object Name,Value | ConvertTo-Json
& netstat.exe -ano | Select-String ':11434'
& curl.exe --silent --max-time 5 http://127.0.0.1:11434/api/version
& wsl.exe --list --verbose
$log = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Ollama\server.log'
if (Test-Path $log) {
    Get-Content $log -Tail 160 | Select-String -Pattern 'version|inference compute|library|GPU|ROCm|Vulkan|offload|runner|error|memory|device' | Select-Object -Last 8
}
