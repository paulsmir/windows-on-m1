param([string]$Out='C:\Users\pavel\realapp\ui-shot.png')
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class AgxDpi { [DllImport("user32.dll")] public static extern bool SetProcessDPIAware(); [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i); }
'@
[AgxDpi]::SetProcessDPIAware() | Out-Null
Add-Type -AssemblyName System.Drawing
$w=[AgxDpi]::GetSystemMetrics(0);$h=[AgxDpi]::GetSystemMetrics(1)
$bmp=New-Object System.Drawing.Bitmap $w,$h
$g=[System.Drawing.Graphics]::FromImage($bmp);$g.CopyFromScreen(0,0,0,0,(New-Object System.Drawing.Size $w,$h))
$bmp.Save($Out,[System.Drawing.Imaging.ImageFormat]::Png);$g.Dispose();$bmp.Dispose()
