param([int]$Seconds=30,[double]$StepMs=8.0)
Add-Type -Namespace W -Name U -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetWindowPos(System.IntPtr h, System.IntPtr a, int x, int y, int cx, int cy, uint f);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);
[DllImport("user32.dll")] public static extern void mouse_event(uint f,int dx,int dy,uint d,System.IntPtr e);
'@
# EXP1110: position-only moves (SWP_NOSIZE) of a Notepad window, like a mouse drag.
[W.U]::SetThreadExecutionState(0x80000003) | Out-Null
[W.U]::mouse_event(1,1,0,0,[IntPtr]::Zero); [W.U]::mouse_event(1,-1,0,0,[IntPtr]::Zero)
Get-Process notepad -EA 0 | Stop-Process -Force; Start-Sleep 1
Start-Process notepad.exe | Out-Null
$h=[IntPtr]::Zero; $p=$null; $w0=Get-Date
while($h -eq [IntPtr]::Zero -and ((Get-Date)-$w0).TotalSeconds -lt 20){ Start-Sleep -Milliseconds 250; $p=Get-Process notepad -EA 0 | ?{ $_.MainWindowHandle -ne [IntPtr]::Zero } | select -First 1; if($p){ $h=$p.MainWindowHandle } }
[W.U]::SetWindowPos($h,[IntPtr]::Zero,400,300,700,500,0x14) | Out-Null; Start-Sleep 2
$t0=Get-Date; $i=0; $sw=[System.Diagnostics.Stopwatch]::StartNew()
while(((Get-Date)-$t0).TotalSeconds -lt $Seconds){ $a=$i*0.08; $x=[int](900+500*[math]::Cos($a)); $y=[int](500+300*[math]::Sin($a)); [W.U]::SetWindowPos($h,[IntPtr]::Zero,$x,$y,0,0,0x15) | Out-Null; [W.U]::SetCursorPos($x+350,$y+250) | Out-Null; $i++; while($sw.Elapsed.TotalMilliseconds -lt $i*$StepMs){ } }
"moves $i hwnd $h" | Out-File $env:USERPROFILE\npmove-done.txt
