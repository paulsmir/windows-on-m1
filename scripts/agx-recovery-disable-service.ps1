# Prepare an ordered GPU-visible recovery boot without unloading the live KMD.
# Microsoft sc-config: disabled cannot be started; after an ordered reboot the
# bound devnode should report CM_PROB_DISABLED_SERVICE (32). This script does
# not perform that reboot or claim its post-boot result.
# https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/sc-config
# https://learn.microsoft.com/en-us/windows-hardware/drivers/install/cm-prob-disabled-service
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidatePattern('^oem[0-9]+\.inf$')][string]$ExpectedPublishedName,
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9A-Fa-f]{64}$')][string]$ExpectedInfSha256,
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9A-Fa-f]{64}$')][string]$ExpectedSysSha256,
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9A-Fa-f]{64}$')][string]$ExpectedUmdSha256,
    [Parameter(Mandatory=$true)][string]$ReceiptPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$id = 'ACPI\APPL0002\0'
$serviceName = 'AppleAgxAdmission'
$deviceKey = 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'
$serviceKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\AppleAgxAdmission'
$sys = Join-Path $env:windir 'System32\drivers\AppleAgxRenderAdmission.sys'
$umd = Join-Path $env:windir 'System32\AppleAgxRenderAdmissionUmd.dll'

function Assert-Hash([string]$Path, [string]$Expected) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "missing exact artifact: $Path" }
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actual -ne $Expected) { throw "identity mismatch: $Path" }
    return $actual
}
function Assert-Disarmed {
    # Both values must be present DWORD zero. Missing/read-error is uncertain,
    # not evidence that a possibly armed next launch is safe.
    $key = Get-Item -LiteralPath $deviceKey
    foreach ($name in @('G3Armed', 'B1Armed')) {
        if ($key.GetValueNames() -notcontains $name -or
            $key.GetValueKind($name) -ne [Microsoft.Win32.RegistryValueKind]::DWord -or
            [int64]$key.GetValue($name) -ne 0) { throw "one-shot arm not proven zero: $name" }
    }
}

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if ($env:COMPUTERNAME -ne 'J313-WIN' -or
    !$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'wrong host or privilege' }
if (!$env:windir -or !(Test-Path -LiteralPath $env:windir -PathType Container)) { throw 'Windows directory absent' }
$devices = @(Get-PnpDevice -PresentOnly | Where-Object InstanceId -like 'ACPI\APPL0002\*')
if ($devices.Count -ne 1 -or $devices[0].InstanceId -ne $id) { throw 'GPU-visible device identity uncertain' }
$bound = (Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverInfPath).Data
$boundService = (Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_Service).Data
if ($bound -ne $ExpectedPublishedName -or $boundService -ne $serviceName) { throw 'bound package/service identity uncertain' }
$staged = @(Get-WindowsDriver -Online -All | Where-Object OriginalFileName -match 'AppleAgx[^\\/]*\.inf$')
if ($staged.Count -ne 1 -or $staged[0].Driver -ne $ExpectedPublishedName -or
    $staged[0].OriginalFileName -notmatch 'AppleAgxRenderAdmission\.inf$') { throw 'staged package identity uncertain' }
$infHash = Assert-Hash (Join-Path $env:windir "INF\$ExpectedPublishedName") $ExpectedInfSha256
$storeInfHash = Assert-Hash $staged[0].OriginalFileName $ExpectedInfSha256
$sysHash = Assert-Hash $sys $ExpectedSysSha256
$umdHash = Assert-Hash $umd $ExpectedUmdSha256
Assert-Disarmed
$drivers = @(Get-CimInstance Win32_SystemDriver -Filter "Name='AppleAgxAdmission'")
if ($drivers.Count -ne 1) { throw 'driver service identity uncertain' }
$startBefore = [int](Get-ItemProperty -LiteralPath $serviceKey -Name Start).Start
$bootUtc = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')

if (!('AgxRecoveryRegistryFlush' -as [type])) {
    Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class AgxRecoveryRegistryFlush {
    [DllImport("advapi32.dll")]
    public static extern int RegFlushKey(SafeRegistryHandle key);
}
'@
}
$systemKey = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('SYSTEM', $false)
if ($null -eq $systemKey) { throw 'SYSTEM hive unavailable' }
$receiptFile = [IO.Path]::GetFullPath($ReceiptPath)
$stream = $null
$result = [ordered]@{
    Version = 1; Utc = [DateTime]::UtcNow.ToString('o'); Computer = $env:COMPUTERNAME;
    BeforeBootUtc = $bootUtc; DeviceId = $id; BeforeProblemCode = [int]$devices[0].Problem;
    PublishedName = $ExpectedPublishedName; BoundInf = $bound; BoundService = $boundService;
    InfSha256 = $infHash; StoreInfSha256 = $storeInfHash; SysSha256 = $sysHash; UmdSha256 = $umdHash;
    G3Armed = 0; B1Armed = 0; StartBefore = $startBefore; StartAfter = $null;
    ServiceStateBefore = $drivers[0].State; MutationAttempted = $false; ScExitCode = $null;
    SystemHiveFlushed = $false; Verdict = 'PENDING';
    NextRequiredAction = 'Parent performs ordered Windows reboot, GPU-visible normal-profile launch, and verifies Code32 plus service Stopped before exact cleanup';
    RecoveryComplete = $false
}
try {
    # Fail before changing service state if a durable, new receipt cannot be
    # opened. Never overwrite a previous recovery receipt or an existing file.
    $stream = [IO.FileStream]::new($receiptFile, [IO.FileMode]::CreateNew,
        [IO.FileAccess]::Write, [IO.FileShare]::Read)
    Assert-Disarmed
    $result.MutationAttempted = $true
    $scOutput = @(& "$env:windir\System32\sc.exe" config $serviceName 'start=' disabled 2>&1 | ForEach-Object { "$_" })
    $result.ScExitCode = $LASTEXITCODE
    $result['ScOutput'] = $scOutput
    if ($result.ScExitCode -ne 0) { throw "sc config failed: $scOutput" }
    $result.StartAfter = [int](Get-ItemProperty -LiteralPath $serviceKey -Name Start).Start
    if ($result.StartAfter -ne 4) { throw 'service Start is not disabled' }
    Assert-Disarmed
    $flush = [AgxRecoveryRegistryFlush]::RegFlushKey($systemKey.Handle)
    if ($flush -ne 0) { throw "SYSTEM hive flush failed: $flush" }
    $result.SystemHiveFlushed = $true
    $result.Verdict = 'PASS'
} catch {
    $result.Verdict = 'FAIL'
    $result['Failure'] = $_.Exception.Message
    throw
} finally {
    $systemKey.Dispose()
    if ($null -ne $stream) {
        try {
            $result.Utc = [DateTime]::UtcNow.ToString('o')
            $json = $result | ConvertTo-Json -Depth 5
            $receiptBytes = [Text.Encoding]::UTF8.GetBytes($json)
            $stream.Write($receiptBytes, 0, $receiptBytes.Length)
            $stream.Flush($true)
        } finally { $stream.Dispose() }
    }
}
# Only emitted after Start=4, SYSTEM flush and receipt Flush(true) all succeed.
Get-Content -LiteralPath $receiptFile -Raw
Write-Output 'AGX_RECOVERY_DISABLE_SERVICE: PASS OrderedRebootRequired'
