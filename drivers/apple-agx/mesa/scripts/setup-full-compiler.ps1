$ErrorActionPreference = 'Stop'
$root = 'C:\Users\pauls\AD04-fullcompiler-001'
$python = 'C:\Users\pauls\AppData\Local\Programs\Python\Python313\python.exe'
if (Test-Path $root) { throw 'Preserve existing compiler build root' }
New-Item -ItemType Directory $root | Out-Null
& $python -m venv "$root\venv"
if ($LASTEXITCODE) { throw 'venv creation failed' }
& "$root\venv\Scripts\python.exe" -m pip install 'meson==1.8.3' 'ninja==1.11.1.4' 'Mako==1.3.10' 'PyYAML==6.0.2' 'packaging==25.0' 'ply==3.11' *> "$root\dependencies.log"
if ($LASTEXITCODE) { throw 'dependency installation failed; inspect dependencies.log' }
& "$root\venv\Scripts\python.exe" -m pip freeze > "$root\dependencies.lock.txt"
Write-Output 'DEPENDENCIES_READY'
