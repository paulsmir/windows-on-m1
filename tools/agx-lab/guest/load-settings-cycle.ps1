param([int]$Cycles=3)
Add-Type -AssemblyName System.Windows.Forms
Add-Type -Namespace W -Name U -MemberDefinition @'
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(System.IntPtr h, System.Text.StringBuilder s, int n);
[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);
'@
# User-like Settings open/close (reported: the screen goes black for a while after closing Settings).
[W.U]::SetThreadExecutionState(0x80000003) | Out-Null
$log=@()
for($c=0;$c -lt $Cycles;$c++){
  $log += 'open {0:o}' -f (Get-Date).ToUniversalTime()
  Start-Process 'ms-settings:'
  Start-Sleep -Seconds 7
  $sb=New-Object System.Text.StringBuilder 256; [W.U]::GetWindowText([W.U]::GetForegroundWindow(),$sb,256) | Out-Null
  $log += 'foreground "{0}" settings {1}' -f $sb.ToString(), ((Get-Process SystemSettings -EA 0).Id -join ',')
  [System.Windows.Forms.SendKeys]::SendWait('%{F4}')
  $log += 'close {0:o}' -f (Get-Date).ToUniversalTime()
  Start-Sleep -Seconds 8
  $log += 'after {0:o} settings {1} dwm {2}' -f (Get-Date).ToUniversalTime(), ((Get-Process SystemSettings -EA 0).Id -join ','), ((Get-Process dwm -EA 0).Id -join ',')
}
$log | Out-File $env:USERPROFILE\settings-done.txt
