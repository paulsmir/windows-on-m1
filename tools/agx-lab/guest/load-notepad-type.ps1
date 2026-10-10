param([int]$Seconds=30)
Add-Type -AssemblyName System.Windows.Forms
Add-Type -Namespace W -Name U -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);
[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);
'@
# EXP1110: continuous typing into a fresh Notepad window (text input rendering).
[W.U]::SetThreadExecutionState(0x80000003) | Out-Null
$p=Get-Process notepad -EA 0 | ?{ $_.MainWindowHandle -ne [IntPtr]::Zero } | select -First 1
if(-not $p){ Start-Process notepad.exe | Out-Null; Start-Sleep 3; $p=Get-Process notepad -EA 0 | ?{ $_.MainWindowHandle -ne [IntPtr]::Zero } | select -First 1 }
[W.U]::SetForegroundWindow($p.MainWindowHandle) | Out-Null; Start-Sleep 1
$t0=Get-Date; $i=0; $text='the quick brown fox jumps over the lazy dog '
while(((Get-Date)-$t0).TotalSeconds -lt $Seconds){ $c=$text[$i % $text.Length]; [System.Windows.Forms.SendKeys]::SendWait([string]$c); if(($i % 60) -eq 59){ [System.Windows.Forms.SendKeys]::SendWait('{ENTER}') }; $i++; Start-Sleep -Milliseconds 15 }
"typed $i" | Out-File $env:USERPROFILE\nptype-done.txt
