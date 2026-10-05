$ErrorActionPreference = 'Stop'
$exe = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs\Ollama\ollama.exe'
$app = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs\Ollama\ollama app.exe'
if (!(Test-Path $exe)) { throw 'Expected installed native Ollama missing' }
Get-Process | Where-Object { $_.Path -eq $app } | Stop-Process
& wsl.exe -d Ubuntu -u root -- systemctl stop ollama
if ($LASTEXITCODE) { throw 'Could not stop conflicting WSL Ollama; no replacement started' }
$listeners = @(Get-NetTCPConnection -State Listen | Where-Object LocalPort -eq 11434)
if ($listeners.Count) { throw 'Port still occupied; no duplicate runtime started' }
$env:OLLAMA_HOST = '127.0.0.1:11434'
$env:OLLAMA_CONTEXT_LENGTH = '4096'
$env:OLLAMA_NUM_PARALLEL = '1'
$env:OLLAMA_KEEP_ALIVE = '10m'
& $exe serve
exit $LASTEXITCODE
