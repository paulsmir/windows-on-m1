[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PackageRoot,
    [Parameter(Mandatory=$true)][string]$EvidenceRoot,
    [Parameter(Mandatory=$true)][string]$ExpectedSysSha256,
    [Parameter(Mandatory=$true)][string]$ExpectedInfSha256,
    [Parameter(Mandatory=$true)][string]$ExpectedCatSha256,
    [Parameter(Mandatory=$true)][string]$ExpectedSignerThumbprint,
    [string]$PreviousPublishedName = ""
)

$ErrorActionPreference = "Stop"
$deviceId = "ACPI\APPL0002\0"
$receiptPrefix = "Wom1"
$startedAt = Get-Date

function Assert-Administrator {
    $principal = New-Object Security.Principal.WindowsPrincipal(
        [Security.Principal.WindowsIdentity]::GetCurrent())
    if (!$principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "Run this script from an elevated PowerShell window."
    }
}

function Normalize-Hex([string]$Value, [int]$Length, [string]$Name) {
    $normalized = $Value.Replace(" ", "").ToUpperInvariant()
    if ($normalized -notmatch "^[0-9A-F]{$Length}$") {
        throw "$Name must contain exactly $Length hexadecimal digits."
    }
    return $normalized
}

function Assert-FileHash([string]$Path, [string]$Expected, [string]$Name) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Name not found: $Path"
    }
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actual -ne $Expected) {
        throw "$Name SHA-256 mismatch: expected $Expected, got $actual"
    }
    return $actual
}

function Invoke-PnpUtil([string[]]$Arguments, [bool]$RequireSuccess) {
    $lines = @(& pnputil @Arguments 2>&1 | ForEach-Object { "$_" })
    $exitCode = $LASTEXITCODE
    if ($RequireSuccess -and $exitCode -ne 0) {
        throw "pnputil $($Arguments -join ' ') failed with exit code $exitCode"
    }
    return [ordered]@{ Arguments = $Arguments; ExitCode = $exitCode; Output = $lines }
}

function Get-DeviceSnapshot {
    $device = @(Get-PnpDevice -PresentOnly -InstanceId $deviceId -ErrorAction SilentlyContinue)
    if ($device.Count -ne 1) {
        throw "Expected exactly one present $deviceId, found $($device.Count)."
    }
    $properties = @{}
    foreach ($key in @(
            "DEVPKEY_Device_ProblemCode",
            "DEVPKEY_Device_DriverInfPath",
            "DEVPKEY_Device_DriverVersion",
            "DEVPKEY_Device_Service")) {
        $value = Get-PnpDeviceProperty -InstanceId $deviceId -KeyName $key `
            -ErrorAction SilentlyContinue
        $properties[$key] = if ($value) { $value.Data } else { $null }
    }
    return [ordered]@{
        Status = $device[0].Status
        Problem = $device[0].Problem
        Class = $device[0].Class
        FriendlyName = $device[0].FriendlyName
        Properties = $properties
    }
}

function Get-HealthSnapshot {
    $services = @{}
    foreach ($name in @("AppleInput", "stornvme", "USBXHCI")) {
        $service = Get-Service -Name $name -ErrorAction SilentlyContinue
        $services[$name] = if ($service) { "$($service.Status)" } else { "Absent" }
    }
    return [ordered]@{
        LogicalProcessors = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
        Services = $services
    }
}

function Assert-PlatformHealth($Health) {
    if ($Health.LogicalProcessors -ne 8) {
        throw "Expected 8 logical processors, got $($Health.LogicalProcessors)."
    }
    foreach ($name in @("AppleInput", "stornvme", "USBXHCI")) {
        if ($Health.Services[$name] -ne "Running") {
            throw "$name is not Running."
        }
    }
}

function Get-LifecycleReceipts {
    $path = "HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters"
    if (!(Test-Path -LiteralPath $path)) {
        return @{}
    }
    $item = Get-ItemProperty -LiteralPath $path
    $receipts = @{}
    foreach ($property in $item.PSObject.Properties) {
        if ($property.Name.StartsWith($receiptPrefix)) {
            $receipts[$property.Name] = $property.Value
        }
    }
    return $receipts
}

function Clear-LifecycleReceipts {
    $path = "HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters"
    if (!(Test-Path -LiteralPath $path)) {
        return
    }
    $item = Get-ItemProperty -LiteralPath $path
    foreach ($property in $item.PSObject.Properties) {
        if ($property.Name.StartsWith($receiptPrefix)) {
            Remove-ItemProperty -LiteralPath $path -Name $property.Name
        }
    }
}

Assert-Administrator
$expectedSys = Normalize-Hex $ExpectedSysSha256 64 "ExpectedSysSha256"
$expectedInf = Normalize-Hex $ExpectedInfSha256 64 "ExpectedInfSha256"
$expectedCat = Normalize-Hex $ExpectedCatSha256 64 "ExpectedCatSha256"
$expectedSigner = Normalize-Hex $ExpectedSignerThumbprint 40 "ExpectedSignerThumbprint"

$package = (Resolve-Path -LiteralPath $PackageRoot).Path
$inf = Join-Path $package "AppleAgx.inf"
$sys = Join-Path $package "AppleAgx.sys"
$cat = Join-Path $package "appleagx.cat"
$hashes = [ordered]@{
    Inf = Assert-FileHash $inf $expectedInf "AppleAgx.inf"
    Sys = Assert-FileHash $sys $expectedSys "AppleAgx.sys"
    Cat = Assert-FileHash $cat $expectedCat "appleagx.cat"
}
$signature = Get-AuthenticodeSignature -FilePath $cat
if ($signature.Status -ne "Valid" -or !$signature.SignerCertificate) {
    throw "appleagx.cat does not have a valid trusted signature."
}
$actualSigner = $signature.SignerCertificate.Thumbprint.ToUpperInvariant()
if ($actualSigner -ne $expectedSigner) {
    throw "Catalog signer mismatch: expected $expectedSigner, got $actualSigner"
}

$beforeHealth = Get-HealthSnapshot
Assert-PlatformHealth $beforeHealth
$beforeDevice = Get-DeviceSnapshot
$beforeReceipts = Get-LifecycleReceipts
Clear-LifecycleReceipts

$runName = "{0:yyyyMMddTHHmmss.fffZ}-{1}" -f `
    $startedAt.ToUniversalTime(), ([Guid]::NewGuid().ToString("N").Substring(0, 8))
$runDirectory = Join-Path (New-Item -ItemType Directory -Force -Path $EvidenceRoot).FullName $runName
New-Item -ItemType Directory -Path $runDirectory | Out-Null

$operations = @()
if ($PreviousPublishedName) {
    if ($PreviousPublishedName -notmatch '^oem\d+\.inf$') {
        throw "PreviousPublishedName must be the exact recorded oemNN.inf identity."
    }
    $operations += Invoke-PnpUtil @("/remove-device", $deviceId) $true
    $operations += Invoke-PnpUtil @("/delete-driver", $PreviousPublishedName) $true
    $operations += Invoke-PnpUtil @("/scan-devices") $true
}

$installOperation = Invoke-PnpUtil @("/add-driver", $inf, "/install") $false
$operations += $installOperation
if ($installOperation.ExitCode -ne 0) {
    # pnputil returns ERROR_NO_MORE_ITEMS (259) when the exact staged package
    # is already the best installed package.  Accept that idempotent state
    # only when APPL0002 is currently bound to a recorded oemNN.inf.
    $boundBeforeRestart = Get-DeviceSnapshot
    $boundInf = $boundBeforeRestart.Properties["DEVPKEY_Device_DriverInfPath"]
    if ($installOperation.ExitCode -ne 259 -or $boundInf -notmatch '^oem\d+\.inf$') {
        throw "pnputil add/install failed with exit code $($installOperation.ExitCode): $($installOperation.Output -join '; ')"
    }
    Write-Host "pnputil 259: APPL0002 already the exact installed package $boundInf"
}
$operations += Invoke-PnpUtil @("/scan-devices") $true
$operations += Invoke-PnpUtil @("/restart-device", $deviceId) $false
Start-Sleep -Seconds 8

$afterDevice = Get-DeviceSnapshot
$afterHealth = Get-HealthSnapshot
Assert-PlatformHealth $afterHealth
$afterReceipts = Get-LifecycleReceipts
$publishedName = $afterDevice.Properties["DEVPKEY_Device_DriverInfPath"]
if ($publishedName -notmatch '^oem\d+\.inf$') {
    throw "APPL0002 did not report an exact oemNN.inf after installation."
}

# Event 129 is the storage-controller reset gate; any fresh occurrence rejects
# this iteration even if the lifecycle receipts themselves are useful.
$event129 = @(Get-WinEvent -FilterHashtable @{
        LogName = "System"; Id = 129; StartTime = $startedAt
    } -ErrorAction SilentlyContinue | Where-Object ProviderName -eq "stornvme")
$critical = @(Get-WinEvent -FilterHashtable @{
        LogName = "System"; Level = 1; StartTime = $startedAt
    } -ErrorAction SilentlyContinue)

$result = [ordered]@{
    FormatVersion = 1
    StartedAtUtc = $startedAt.ToUniversalTime().ToString("o")
    CompletedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    DeviceId = $deviceId
    Package = [ordered]@{
        Hashes = $hashes
        SignerThumbprint = $actualSigner
        PublishedName = $publishedName
        PreviousPublishedName = $PreviousPublishedName
    }
    Before = [ordered]@{
        Device = $beforeDevice
        Health = $beforeHealth
        Receipts = $beforeReceipts
    }
    Operations = $operations
    After = [ordered]@{
        Device = $afterDevice
        Health = $afterHealth
        Receipts = $afterReceipts
        Event129Count = $event129.Count
        CriticalEventCount = $critical.Count
    }
}
$resultPath = Join-Path $runDirectory "result.json"
$result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $resultPath -Encoding UTF8

if ($event129.Count -ne 0) {
    throw "Rejected: observed $($event129.Count) fresh stornvme Event 129 record(s)."
}
if ($critical.Count -ne 0) {
    throw "Rejected: observed $($critical.Count) fresh critical System event(s)."
}

Write-Host "Lifecycle iteration evidence: $resultPath"
Write-Host "Published name for the next bounded cycle: $publishedName"
