# Recent System events that matter after a run (unexpected shutdown, bugcheck,
# TDR/LiveKernelEvent, display) and any crash dump written in the last 3 hours.
$since = (Get-Date).AddMinutes(-75)
Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=$since} -EA 0 |
  Where-Object { $_.Id -in 41,1001,6008,6005,6006,1074,161,4101,117,141,13,12 } |
  Select-Object -First 25 | ForEach-Object {
    $m = ($_.Message -replace '\s+',' ')
    if ($m.Length -gt 240) { $m = $m.Substring(0,240) }
    '{0} {1} {2} {3}' -f $_.TimeCreated.ToUniversalTime().ToString('HH:mm:ss'), $_.ProviderName, $_.Id, $m }
Get-ChildItem C:\Windows\MEMORY.DMP,C:\Windows\Minidump,C:\Windows\LiveKernelReports -Recurse -EA 0 |
  Where-Object { $_.LastWriteTime -gt (Get-Date).AddHours(-3) } |
  ForEach-Object { '{0} {1} {2}' -f $_.FullName, $_.Length, $_.LastWriteTime.ToUniversalTime().ToString('HH:mm:ss') }
