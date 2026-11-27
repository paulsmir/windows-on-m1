# SPDX-License-Identifier: MIT
# clockcheck.ps1 - READ-ONLY Windows clock evidence for the wall-clock line (Architect order 2026-11-27).
# Run over SSH as:  powershell -NoProfile -ExecutionPolicy Bypass -File clockcheck.ps1
# Reads only: the current UTC, System log events Kernel-General 1/12/13, the ShutdownTime registry value, w32tm
# status/source. Writes nothing (no w32tm /resync, no registry or service change). Compare CLOCKCHECK_NOW with the
# host's `date -u` taken just before the SSH call.
$ErrorActionPreference = 'Continue'
function Out1([string]$s) { Write-Output ("CLOCKCHECK " + $s) }
Out1 ("NOW utc=" + [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ss.fffZ"))

# (a) time changes: Kernel-General 1 ("The system time has changed to <new> from <old>. Change Reason: ...")
try {
  $ev = Get-WinEvent -FilterHashtable @{ LogName = 'System'; ProviderName = 'Microsoft-Windows-Kernel-General'; Id = 1 } -MaxEvents 30 -ErrorAction Stop
  foreach ($e in $ev) {
    $p = @($e.Properties | ForEach-Object { $_.Value })
    $vals = ($p | ForEach-Object { if ($_ -is [DateTime]) { $_.ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ss.fffZ") } else { "$_" } }) -join ' | '
    Out1 ("ID1 logged=" + $e.TimeCreated.ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ss.fffZ") + " props=[" + $vals + "]")
  }
} catch { Out1 ("ID1 none_or_error=" + $_.Exception.Message) }

# (b) boot start (ID 12) and shutdown (ID 13) events, plus the ShutdownTime registry value (FILETIME, UTC)
foreach ($id in 12, 13) {
  try {
    $ev = Get-WinEvent -FilterHashtable @{ LogName = 'System'; ProviderName = 'Microsoft-Windows-Kernel-General'; Id = $id } -MaxEvents 8 -ErrorAction Stop
    foreach ($e in $ev) {
      $p = @($e.Properties | ForEach-Object { $_.Value })
      $vals = ($p | ForEach-Object { if ($_ -is [DateTime]) { $_.ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ss.fffZ") } else { "$_" } }) -join ' | '
      Out1 ("ID" + $id + " logged=" + $e.TimeCreated.ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ss.fffZ") + " props=[" + $vals + "]")
    }
  } catch { Out1 ("ID" + $id + " none_or_error=" + $_.Exception.Message) }
}
try {
  $raw = (Get-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\Windows' -Name ShutdownTime -ErrorAction Stop).ShutdownTime
  $ft = [BitConverter]::ToInt64($raw, 0)
  $hex = ($raw | ForEach-Object { $_.ToString("x2") }) -join ''
  Out1 ("SHUTDOWNTIME utc=" + [DateTime]::FromFileTimeUtc($ft).ToString("yyyy-MM-ddTHH:mm:ss.fffZ") + " raw=" + $hex)
} catch { Out1 ("SHUTDOWNTIME error=" + $_.Exception.Message) }
try { Out1 ("LASTBOOT utc=" + (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ss.fffZ")) }
catch { Out1 ("LASTBOOT error=" + $_.Exception.Message) }

# (c) time service state (query only)
foreach ($q in '/query /status', '/query /source') {
  $o = & w32tm.exe $q.Split(' ') 2>&1
  foreach ($l in $o) { if ("$l".Trim()) { Out1 ("W32TM " + $q + " :: " + "$l".Trim()) } }
}
Out1 "DONE"
