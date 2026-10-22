param([int]$X,[int]$Y,[int]$Wheel=0)
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class AgxMouse {
 [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint dx,uint dy,int d,UIntPtr e);
}
'@
[AgxMouse]::SetProcessDPIAware() | Out-Null
[AgxMouse]::SetCursorPos($X,$Y) | Out-Null; Start-Sleep -Milliseconds 150
if($Wheel){ [AgxMouse]::mouse_event(0x800,0,0,$Wheel,[UIntPtr]::Zero) } else {
[AgxMouse]::mouse_event(2,0,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds 80; [AgxMouse]::mouse_event(4,0,0,0,[UIntPtr]::Zero) }
