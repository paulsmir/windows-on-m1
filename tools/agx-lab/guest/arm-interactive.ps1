# Start a guest script in the interactive desktop session through a scheduled
# task: loads must drive the logged-on user's desktop, which an SSH session
# cannot reach. -Arguments carries positional arguments only (a value that starts
# with '-' would be parsed as a parameter of this script).
param([Parameter(Mandatory = $true)][string]$Script, [string]$Arguments = '', [string]$TaskName = '')
if (-not $TaskName) { $TaskName = 'Agx' + ([IO.Path]::GetFileNameWithoutExtension($Script) -replace '[^A-Za-z0-9]', '') }
$u = (Get-CimInstance Win32_ComputerSystem).UserName
$act = New-ScheduledTaskAction -Execute conhost.exe -Argument "--headless powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File $Script $Arguments"
$pr = New-ScheduledTaskPrincipal -UserId $u -LogonType Interactive -RunLevel Limited
Register-ScheduledTask -TaskName $TaskName -Action $act -Principal $pr -Force | Out-Null
Start-ScheduledTask -TaskName $TaskName
"$TaskName armed for $u"
