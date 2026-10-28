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
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 64)] public byte[] dmDeviceName;
    public ushort dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
    public uint dmFields;
    public int dmPositionX, dmPositionY;
    public uint dmDisplayOrientation, dmDisplayFixedOutput;
    public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 64)] public byte[] dmFormName;
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
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  public struct DISPLAY_DEVICEW {
    public uint cb;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 64)] public byte[] DeviceNameRaw;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)] public byte[] DeviceStringRaw;
    public uint StateFlags;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)] public byte[] DeviceIDRaw;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)] public byte[] DeviceKeyRaw;
    public string DeviceName { get { return Text(DeviceNameRaw); } }
    public string DeviceString { get { return Text(DeviceStringRaw); } }
  }
  public static string Text(byte[] raw) {
    if (raw == null) return "";
    string s = System.Text.Encoding.Unicode.GetString(raw);
    int z = s.IndexOf('\0');
    return z >= 0 ? s.Substring(0, z) : s;
  }
  public const uint DISPLAY_DEVICE_ATTACHED_TO_DESKTOP = 0x1, DISPLAY_DEVICE_PRIMARY_DEVICE = 0x4;
  [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
  public static extern bool EnumDisplayDevicesW(string device, uint index, ref DISPLAY_DEVICEW dd, uint flags);
  [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
  public static extern bool EnumDisplaySettingsW(string device, int mode, ref DEVMODEW dm);
  [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
  public static extern int ChangeDisplaySettingsExW(string device, ref DEVMODEW dm,
      IntPtr hwnd, uint flags, IntPtr param);
  [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "ChangeDisplaySettingsExW", SetLastError = true)]
  public static extern int RestoreDisplaySettings(string device, IntPtr dm,
      IntPtr hwnd, uint flags, IntPtr param);
  [DllImport("kernel32.dll")] public static extern uint GetLastError();
  [DllImport("kernel32.dll")] public static extern IntPtr GetCurrentProcess();
  [DllImport("kernel32.dll")] public static extern bool ProcessIdToSessionId(uint pid, out uint session);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentProcessId();
  [DllImport("user32.dll")] public static extern IntPtr GetProcessWindowStation();
  [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
  public static extern bool GetUserObjectInformationW(IntPtr h, int index, System.Text.StringBuilder info, int length, out int needed);
  // EXP2053: ByValTStr under Add-Type sized DEVMODEW as 188 bytes instead of
  // 220, so EnumDisplaySettingsW failed with error 203 and every CDS call
  // with BADPARAM. Raw UTF-16 byte arrays keep the Win32 sizes exact.
  public const int DevModeSize = 220, DisplayDeviceSize = 840;
  public static DISPLAY_DEVICEW EmptyDevice() {
    DISPLAY_DEVICEW dd = new DISPLAY_DEVICEW();
    dd.DeviceNameRaw = new byte[64]; dd.DeviceStringRaw = new byte[256];
    dd.DeviceIDRaw = new byte[256]; dd.DeviceKeyRaw = new byte[256];
    dd.cb = (uint)Marshal.SizeOf(typeof(DISPLAY_DEVICEW));
    return dd;
  }
  public static DEVMODEW Empty() {
    DEVMODEW dm = new DEVMODEW();
    dm.dmDeviceName = new byte[64]; dm.dmFormName = new byte[64];
    dm.dmSize = (ushort)Marshal.SizeOf(typeof(DEVMODEW));
    return dm;
  }
}
'@
# EXP2049: the probe reported no modes at all ("0x0@0") while GravityMark ran
# in the same session; record why (Win32 error, window station, session) and
# enumerate through the primary device's explicit name as well as NULL.
function Current([string]$device) { $m=[AgxDisplayProbe]::Empty(); $ok=[AgxDisplayProbe]::EnumDisplaySettingsW($device,[AgxDisplayProbe]::ENUM_CURRENT_SETTINGS,[ref]$m); $e=[System.Runtime.InteropServices.Marshal]::GetLastWin32Error(); "$($m.dmPelsWidth)x$($m.dmPelsHeight)@$($m.dmDisplayFrequency) $($m.dmBitsPerPel)bpp fixed=$($m.dmDisplayFixedOutput) ok=$ok err=$e size=$($m.dmSize)" }
function ModeList([string]$device) { $list=@(); $i=0
  while($true){ $m=[AgxDisplayProbe]::Empty(); if(-not [AgxDisplayProbe]::EnumDisplaySettingsW($device,$i,[ref]$m)){ $script:lastEnumError=[System.Runtime.InteropServices.Marshal]::GetLastWin32Error(); break }
    $list+="$($m.dmPelsWidth)x$($m.dmPelsHeight)@$($m.dmDisplayFrequency) $($m.dmBitsPerPel)bpp fixed=$($m.dmDisplayFixedOutput)"; $i++; if($i -gt 512){break} }
  return ,$list }
$devices=@(); $di=0; $primary=$null
while($true){ $dd=[AgxDisplayProbe]::EmptyDevice(); if(-not [AgxDisplayProbe]::EnumDisplayDevicesW($null,$di,[ref]$dd,0)){break}
  $devices+="$($dd.DeviceName) flags=0x$('{0:x}' -f $dd.StateFlags) $($dd.DeviceString)"
  if(($dd.StateFlags -band [AgxDisplayProbe]::DISPLAY_DEVICE_PRIMARY_DEVICE) -ne 0){ $primary=$dd.DeviceName }
  $di++; if($di -gt 16){break} }
$session=[uint32]0; [void][AgxDisplayProbe]::ProcessIdToSessionId([AgxDisplayProbe]::GetCurrentProcessId(),[ref]$session)
$winsta=New-Object System.Text.StringBuilder 256; $needed=0
[void][AgxDisplayProbe]::GetUserObjectInformationW([AgxDisplayProbe]::GetProcessWindowStation(),2,$winsta,256,[ref]$needed)
$script:lastEnumError=0
$modes=ModeList $null; $nullEnumError=$script:lastEnumError
$modesByName=@(); $nameEnumError=0
if($primary){ $modesByName=ModeList $primary; $nameEnumError=$script:lastEnumError }
$r=[ordered]@{Tag=$Tag;DevModeSize=[System.Runtime.InteropServices.Marshal]::SizeOf([type][AgxDisplayProbe+DEVMODEW]);
  DisplayDeviceSize=[System.Runtime.InteropServices.Marshal]::SizeOf([type][AgxDisplayProbe+DISPLAY_DEVICEW]);Session=$session;WindowStation=$winsta.ToString();Devices=$devices;PrimaryDevice=$primary;
  Before=(Current $null);BeforeByName=$(if($primary){Current $primary}else{''});
  ModeCount=$modes.Count;NullEnumError=$nullEnumError;Modes=($modes|Select-Object -Unique);
  ModeCountByName=$modesByName.Count;NameEnumError=$nameEnumError;ModesByName=($modesByName|Select-Object -Unique);
  Requested="${Width}x${Height}"}
if(-not $ListOnly){
  $want=[AgxDisplayProbe]::Empty()
  $want.dmPelsWidth=[uint32]$Width; $want.dmPelsHeight=[uint32]$Height; $want.dmBitsPerPel=32
  $want.dmFields=[AgxDisplayProbe]::DM_PELSWIDTH -bor [AgxDisplayProbe]::DM_PELSHEIGHT -bor [AgxDisplayProbe]::DM_BITSPERPEL
  # DISP_CHANGE_SUCCESSFUL=0, BADMODE=-2, FAILED=-1, BADPARAM=-5
  $r.TestResult=[AgxDisplayProbe]::ChangeDisplaySettingsExW($null,[ref]$want,[IntPtr]::Zero,[AgxDisplayProbe]::CDS_TEST,[IntPtr]::Zero)
  $r.TestError=[System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
  $cdsDevice=$null
  if($r.TestResult -ne 0 -and $primary){
    $r.TestResultByName=[AgxDisplayProbe]::ChangeDisplaySettingsExW($primary,[ref]$want,[IntPtr]::Zero,[AgxDisplayProbe]::CDS_TEST,[IntPtr]::Zero)
    $r.TestErrorByName=[System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
    if($r.TestResultByName -eq 0){ $r.TestResult=0; $cdsDevice=$primary }
  }
  if($r.TestResult -eq 0){
    $t0=Get-Date
    $r.ApplyResult=[AgxDisplayProbe]::ChangeDisplaySettingsExW($cdsDevice,[ref]$want,[IntPtr]::Zero,[AgxDisplayProbe]::CDS_FULLSCREEN,[IntPtr]::Zero)
    $r.ApplyError=[System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
    $r.ApplyMs=[int]((Get-Date)-$t0).TotalMilliseconds
    $r.During=(Current $cdsDevice)
    Start-Sleep -Seconds $HoldSeconds
    $r.DwmAliveDuring=[bool](Get-Process dwm -ErrorAction SilentlyContinue)
  }
  # Always return to the registry (desktop) mode, even after a failed apply.
  $r.RestoreResult=[AgxDisplayProbe]::RestoreDisplaySettings($cdsDevice,[IntPtr]::Zero,[IntPtr]::Zero,0,[IntPtr]::Zero)
  $r.RestoreError=[System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
  Start-Sleep -Seconds 3
  $r.After=(Current $cdsDevice)
  $r.DwmAliveAfter=[bool](Get-Process dwm -ErrorAction SilentlyContinue)
  $r.DisplayEvents=@(Get-WinEvent -FilterHashtable @{LogName='System';StartTime=(Get-Date).AddMinutes(-5)} -ErrorAction SilentlyContinue |
    Where-Object { $_.ProviderName -match 'Display|dxgkrnl|LiveKernelEvent' -or $_.Id -eq 4101 } | ForEach-Object { "$($_.Id) $($_.ProviderName)" })
}
try {
  $dev=Get-PnpDevice -InstanceId 'ACPI\APPL0002\0' -ErrorAction Stop
  $key="HKLM:\SYSTEM\CurrentControlSet\Enum\$($dev.InstanceId)\Device Parameters"
  $v=(Get-ItemProperty -Path $key -Name Wom1DisplayModesReceipt -ErrorAction Stop).Wom1DisplayModesReceipt
  $r.Wom1DisplayModesReceipt=($v | ForEach-Object { '{0:x2}' -f $_ }) -join ''
} catch { $r.Wom1DisplayModesReceipt="unavailable: $($_.Exception.Message)" }
$r | ConvertTo-Json -Depth 3 | Out-File -Encoding utf8 "$d\$Tag-modes.json"
$r | ConvertTo-Json -Depth 3
