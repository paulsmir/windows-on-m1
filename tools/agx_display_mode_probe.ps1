# rc106 display-mode probe. Run in the interactive session (like
# agx_gravitymark_run.ps1): ChangeDisplaySettingsExW works only on the
# console desktop. It lists the modes Windows offers for the primary display,
# tests and applies one fullscreen (non-persisted) mode, holds it, checks DWM,
# and always restores the registry mode. Result: <Tag>-modes.json.
[CmdletBinding()]
param([string]$Tag='modes',[int]$Width=1920,[int]$Height=1080,[int]$HoldSeconds=10,[int]$ListOnly=0)
$d='C:\Users\pavel\gravitymark\runs'; New-Item -ItemType Directory -Force $d | Out-Null
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class AgxDisplayProbe {
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  public struct DEVMODEW {
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
    public ushort dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
    public uint dmFields;
    public int dmPositionX, dmPositionY;
    public uint dmDisplayOrientation, dmDisplayFixedOutput;
    public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
    public ushort dmLogPixels;
    public uint dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags,
                dmDisplayFrequency, dmICMMethod, dmICMIntent, dmMediaType,
                dmDitherType, dmReserved1, dmReserved2, dmPanningWidth,
                dmPanningHeight;
  }
  public const int ENUM_CURRENT_SETTINGS = -1;
  public const uint DM_BITSPERPEL = 0x40000, DM_PELSWIDTH = 0x80000,
                    DM_PELSHEIGHT = 0x100000;
  public const uint CDS_FULLSCREEN = 0x4, CDS_TEST = 0x2;
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern bool EnumDisplaySettingsW(string device, int mode, ref DEVMODEW dm);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern int ChangeDisplaySettingsExW(string device, ref DEVMODEW dm,
      IntPtr hwnd, uint flags, IntPtr param);
  [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "ChangeDisplaySettingsExW")]
  public static extern int RestoreDisplaySettings(string device, IntPtr dm,
      IntPtr hwnd, uint flags, IntPtr param);
  public static DEVMODEW Empty() {
    DEVMODEW dm = new DEVMODEW();
    dm.dmSize = (ushort)Marshal.SizeOf(typeof(DEVMODEW));
    return dm;
  }
}
'@
function Current { $m=[AgxDisplayProbe]::Empty(); [void][AgxDisplayProbe]::EnumDisplaySettingsW($null,[AgxDisplayProbe]::ENUM_CURRENT_SETTINGS,[ref]$m); "$($m.dmPelsWidth)x$($m.dmPelsHeight)@$($m.dmDisplayFrequency) $($m.dmBitsPerPel)bpp" }
$modes=@(); $i=0
while($true){ $m=[AgxDisplayProbe]::Empty(); if(-not [AgxDisplayProbe]::EnumDisplaySettingsW($null,$i,[ref]$m)){break}
  $modes+="$($m.dmPelsWidth)x$($m.dmPelsHeight)@$($m.dmDisplayFrequency) $($m.dmBitsPerPel)bpp fixed=$($m.dmDisplayFixedOutput)"; $i++ }
$r=[ordered]@{Tag=$Tag;Before=(Current);ModeCount=$modes.Count;Modes=($modes|Select-Object -Unique);Requested="${Width}x${Height}"}
if(-not $ListOnly){
  $want=[AgxDisplayProbe]::Empty()
  $want.dmPelsWidth=[uint32]$Width; $want.dmPelsHeight=[uint32]$Height; $want.dmBitsPerPel=32
  $want.dmFields=[AgxDisplayProbe]::DM_PELSWIDTH -bor [AgxDisplayProbe]::DM_PELSHEIGHT -bor [AgxDisplayProbe]::DM_BITSPERPEL
  # DISP_CHANGE_SUCCESSFUL=0, BADMODE=-2, FAILED=-1
  $r.TestResult=[AgxDisplayProbe]::ChangeDisplaySettingsExW($null,[ref]$want,[IntPtr]::Zero,[AgxDisplayProbe]::CDS_TEST,[IntPtr]::Zero)
  if($r.TestResult -eq 0){
    $t0=Get-Date
    $r.ApplyResult=[AgxDisplayProbe]::ChangeDisplaySettingsExW($null,[ref]$want,[IntPtr]::Zero,[AgxDisplayProbe]::CDS_FULLSCREEN,[IntPtr]::Zero)
    $r.ApplyMs=[int]((Get-Date)-$t0).TotalMilliseconds
    $r.During=(Current)
    Start-Sleep -Seconds $HoldSeconds
    $r.DwmAliveDuring=[bool](Get-Process dwm -ErrorAction SilentlyContinue)
  }
  # Always return to the registry (desktop) mode, even after a failed apply.
  $r.RestoreResult=[AgxDisplayProbe]::RestoreDisplaySettings($null,[IntPtr]::Zero,[IntPtr]::Zero,0,[IntPtr]::Zero)
  Start-Sleep -Seconds 3
  $r.After=(Current)
  $r.DwmAliveAfter=[bool](Get-Process dwm -ErrorAction SilentlyContinue)
  $r.DisplayEvents=@(Get-WinEvent -FilterHashtable @{LogName='System';StartTime=(Get-Date).AddMinutes(-5)} -ErrorAction SilentlyContinue |
    Where-Object { $_.ProviderName -match 'Display|dxgkrnl|LiveKernelEvent' -or $_.Id -eq 4101 } | ForEach-Object { "$($_.Id) $($_.ProviderName)" })
}
$r | ConvertTo-Json -Depth 3 | Out-File -Encoding utf8 "$d\$Tag-modes.json"
$r | ConvertTo-Json -Depth 3
