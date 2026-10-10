param([int]$Seconds=30,[double]$StepMs=8.0)
Add-Type -Namespace W -Name U -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetWindowPos(System.IntPtr h, System.IntPtr a, int x, int y, int cx, int cy, uint f);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
[DllImport("user32.dll")] public static extern System.IntPtr FindWindow(string c, string t);
[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);
'@
# EXP1110: position-only moves of a File Explorer window.
[W.U]::SetThreadExecutionState(0x80000003) | Out-Null
Start-Process explorer.exe 'C:\Windows' | Out-Null; Start-Sleep 4
$h=[W.U]::FindWindow('CabinetWClass',$null)
[W.U]::SetWindowPos($h,[IntPtr]::Zero,400,300,900,600,0x14) | Out-Null; Start-Sleep 2
$t0=Get-Date; $i=0; $sw=[System.Diagnostics.Stopwatch]::StartNew()
while(((Get-Date)-$t0).TotalSeconds -lt $Seconds){ $a=$i*0.08; $x=[int](800+400*[math]::Cos($a)); $y=[int](400+250*[math]::Sin($a)); [W.U]::SetWindowPos($h,[IntPtr]::Zero,$x,$y,0,0,0x15) | Out-Null; [W.U]::SetCursorPos($x+450,$y+300) | Out-Null; $i++; while($sw.Elapsed.TotalMilliseconds -lt $i*$StepMs){ } }
"moves $i hwnd $h" | Out-File $env:USERPROFILE\exmove-done.txt
