$ErrorActionPreference='Stop'
$d=Get-PnpDevice -InstanceId 'ACPI\APPL0002\0'
if([int]$d.Problem -ne 0){throw 'expected Code0'}
. 'C:\Users\pavel\EXP865\etl-contract.ps1'
$boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')
$receipt=Get-Content 'C:\Users\pavel\EXP865-evidence\original-etl-receipt.json' -Raw|ConvertFrom-Json
Assert-OriginalEtlReceipt $receipt $boot 'C:\Users\pavel\EXP865-evidence'
$r=[ordered]@{Utc=[DateTime]::UtcNow.ToString('o');Experiment='EXP865';Problem=[int]$d.Problem;Action='OrderedRestart';NextProfile='immutable-GPU-hidden-Code45';NoLiveRemoval=$true}
$r|ConvertTo-Json -Compress|Set-Content 'C:\Users\pavel\EXP865\recovery-restart.json'
& shutdown.exe /r /t 10
if($LASTEXITCODE){throw 'restart refused'}
Get-Content 'C:\Users\pavel\EXP865\recovery-restart.json'
