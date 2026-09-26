[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ExpectedPublishedName,
    [Parameter(Mandatory=$true)][string]$ExpectedInfSha256,
    [Parameter(Mandatory=$true)][string]$ExpectedSysSha256,
    [Parameter(Mandatory=$true)][string]$ExpectedUmdSha256,
    [Parameter(Mandatory=$true)][string]$ExpectedSignerThumbprint,
    [Parameter(Mandatory=$true)][string]$ReceiptPath,
    [int]$ExpectedProblemCode = 43
)

$ErrorActionPreference = 'Stop'
$id = 'ACPI\APPL0002\0'
$key = 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'
$sys = 'C:\Windows\System32\drivers\AppleAgxRenderAdmission.sys'
$umd = 'C:\Windows\System32\AppleAgxRenderAdmissionUmd.dll'

function Assert-Hex([string]$Value, [int]$Digits) {
    if ($Value -cnotmatch "^[0-9A-Fa-f]{$Digits}$") { throw "invalid exact hash or thumbprint: $Value" }
}
function Assert-Hash([string]$Path, [string]$Expected) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "missing exact artifact: $Path" }
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actual -ne $Expected) { throw "identity mismatch: $Path" }
}

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -or
    $env:COMPUTERNAME -ne 'J313-WIN') { throw 'wrong host or privilege' }
if ($ExpectedPublishedName -notmatch '^oem[0-9]+\.inf$') { throw 'invalid exact published name' }
foreach ($hash in @($ExpectedInfSha256, $ExpectedSysSha256, $ExpectedUmdSha256)) { Assert-Hex $hash 64 }
Assert-Hex $ExpectedSignerThumbprint 40
$device = Get-PnpDevice -InstanceId $id -ErrorAction SilentlyContinue
if (!$device -or [int]$device.Problem -ne $ExpectedProblemCode) { throw 'unexpected device state' }
if ((Get-ItemProperty $key -Name G3Armed -ErrorAction SilentlyContinue).G3Armed -or
    (Get-ItemProperty $key -Name B1Armed -ErrorAction SilentlyContinue).B1Armed) { throw 'one-shot arm remains' }
$staged = @(Get-WindowsDriver -Online -All | Where-Object OriginalFileName -match 'AppleAgxRenderAdmission\.inf$')
if ($staged.Count -ne 1 -or $staged[0].Driver -ne $ExpectedPublishedName) { throw 'staged identity uncertain' }
$bound = (Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverInfPath -ErrorAction SilentlyContinue).Data
if ($bound -ne $ExpectedPublishedName) { throw 'bound identity uncertain' }
Assert-Hash (Join-Path $env:windir "INF\$ExpectedPublishedName") $ExpectedInfSha256
Assert-Hash $sys $ExpectedSysSha256
Assert-Hash $umd $ExpectedUmdSha256
$service = Get-CimInstance Win32_SystemDriver -Filter "Name='AppleAgxAdmission'" -ErrorAction SilentlyContinue
if ($service -and $service.State -eq 'Running') { throw 'service still running' }

# EXP821: deleting the package while APPL0002 still existed left a phantom
# devnode that rebound oem5 on the next two ordinary boots.
$remove = @(& pnputil.exe /remove-device $id 2>&1 | ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 0) { throw "remove-device failed: $remove" }
$remaining = Get-PnpDevice -InstanceId $id -ErrorAction SilentlyContinue
if ($remaining -and [int]$remaining.Problem -eq 45) {
    # PnP can retain the just-removed instance as a non-present phantom.
    # EXP821 demonstrated that deleting the package while it exists allows
    # that phantom to rebind on a later ordinary boot.
    $phantomRemove = @(& pnputil.exe /remove-device $id 2>&1 | ForEach-Object { "$_" })
    if ($LASTEXITCODE -ne 0) { throw "phantom remove-device failed: $phantomRemove" }
    $remove += $phantomRemove
    $remaining = Get-PnpDevice -InstanceId $id -ErrorAction SilentlyContinue
}
if ($remaining) { throw 'phantom devnode remains' }
$delete = @(& pnputil.exe /delete-driver $ExpectedPublishedName /uninstall 2>&1 | ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 3010) { throw "delete-driver failed: $delete" }
if (@(Get-WindowsDriver -Online -All | Where-Object OriginalFileName -match 'AppleAgxRenderAdmission\.inf$').Count -ne 0) {
    throw 'staged package remains'
}
foreach ($file in @($sys, $umd)) {
    if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force }
}
if ($service) { & sc.exe delete AppleAgxAdmission | Out-Null }
foreach ($store in @('Root', 'TrustedPublisher')) {
    $cert = "Cert:\LocalMachine\$store\$ExpectedSignerThumbprint"
    if (Test-Path $cert) { Remove-Item $cert -Force }
}
[Environment]::SetEnvironmentVariable('APPLE_AGX_UMD_TRACE_FILE', $null, 'Machine')
$result = [ordered]@{ Utc = [DateTime]::UtcNow.ToString('o'); DeviceId = $id;
    PublishedName = $ExpectedPublishedName; Remove = $remove; Delete = $delete;
    Staged = 0; Arm = $null; Sys = (Test-Path $sys); Umd = (Test-Path $umd);
    CleanupComplete = $false; Durability = 'PendingOrderedGuestRestart';
    NextRequiredAction = 'Restart Windows orderly, verify package and phantom absent in GPU-hidden guest, then boot ordinary Code28' }
$result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $ReceiptPath -Encoding UTF8
if ($result.Sys -or $result.Umd) { throw 'exact driver files remain' }
Get-Content -LiteralPath $ReceiptPath
