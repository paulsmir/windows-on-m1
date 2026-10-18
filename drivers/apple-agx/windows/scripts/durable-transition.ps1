[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('Commit','Preflight')][string]$Mode,
    [Parameter(Mandatory=$true)][string]$ManifestPath
)

$ErrorActionPreference = 'Stop'
$manifestFile = (Resolve-Path -LiteralPath $ManifestPath).Path
$manifest = Get-Content -LiteralPath $manifestFile -Raw | ConvertFrom-Json
$transitionFile = "$manifestFile.transition.json"
if ($env:COMPUTERNAME -ne 'J313-WIN') { throw 'wrong guest' }
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'administrator required' }

function Assert-ScriptHashes {
    if (!$manifest.Scripts -or @($manifest.Scripts.PSObject.Properties).Count -eq 0) {
        throw 'script hash manifest missing'
    }
    foreach ($entry in $manifest.Scripts.PSObject.Properties) {
        if ($entry.Value -notmatch '^[0-9a-fA-F]{64}$' -or !(Test-Path -LiteralPath $entry.Name -PathType Leaf)) {
            throw "script absent or hash invalid: $($entry.Name)"
        }
        if ((Get-FileHash -LiteralPath $entry.Name -Algorithm SHA256).Hash -ne $entry.Value) {
            throw "script hash mismatch: $($entry.Name)"
        }
    }
}

function Assert-PackageState {
    if ($manifest.Package.State -notin @('Staged','Absent')) { throw 'package state missing' }
    $staged = @(Get-WindowsDriver -Online -All |
        Where-Object OriginalFileName -match 'AppleAgxRenderAdmission\.inf$')
    if ($manifest.Package.State -eq 'Staged') {
        if ($manifest.Package.PublishedName -notmatch '^oem[0-9]+\.inf$' -or
            $manifest.Package.InfSha256 -notmatch '^[0-9a-fA-F]{64}$' -or
            $staged.Count -ne 1 -or $staged[0].Driver -ne $manifest.Package.PublishedName) {
            throw 'staged package identity mismatch'
        }
        $inf = Join-Path $env:windir "INF\$($staged[0].Driver)"
        if ((Get-FileHash -LiteralPath $inf -Algorithm SHA256).Hash -ne $manifest.Package.InfSha256) {
            throw 'staged INF hash mismatch'
        }
    } elseif ($staged.Count -ne 0) {
        throw 'AppleAgx package remains staged'
    }
    if ($manifest.Package.SignerThumbprint -notmatch '^[0-9a-fA-F]{40}$' -or
        $null -eq $manifest.Package.SignerPresent) { throw 'signer expectation missing' }
    foreach ($store in @('Root','TrustedPublisher')) {
        $present = Test-Path "Cert:\LocalMachine\$store\$($manifest.Package.SignerThumbprint)"
        if ($present -ne [bool]$manifest.Package.SignerPresent) { throw 'signer state mismatch' }
    }
    $key = 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'
    $arm = (Get-ItemProperty -Path $key -Name G3Armed -ErrorAction SilentlyContinue).G3Armed
    if ([int]$arm -ne [int]$manifest.Package.G3Armed) { throw 'G3Armed state mismatch' }
    $device = Get-PnpDevice -InstanceId 'ACPI\APPL0002\0' -ErrorAction SilentlyContinue
    if ($manifest.Package.State -eq 'Staged') {
        $inf = if ($device) { (Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName DEVPKEY_Device_DriverInfPath -ErrorAction SilentlyContinue).Data } else { $null }
        $service = if ($device) { (Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName DEVPKEY_Device_Service -ErrorAction SilentlyContinue).Data } else { $null }
        if (!$device -or [int]$device.Problem -ne 28 -or $inf -or $service) {
            throw 'unexpected live bind after ordered reboot'
        }
    }
    if ($manifest.Package.State -eq 'Absent' -and $device -and [int]$device.Problem -eq 45) {
        throw 'phantom APPL0002 remains'
    }
    if ($manifest.Package.State -eq 'Absent') {
        if ((Test-Path 'C:\Windows\System32\drivers\AppleAgxRenderAdmission.sys') -or
            (Test-Path 'C:\Windows\System32\AppleAgxRenderAdmissionUmd.dll')) {
            throw 'active AppleAgx file remains'
        }
    }
}

function Get-BootUtc {
    return (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')
}

Assert-ScriptHashes
Assert-PackageState
$manifestHash = (Get-FileHash -LiteralPath $manifestFile -Algorithm SHA256).Hash
if ($Mode -eq 'Commit') {
    $transition = [ordered]@{
        ManifestSha256 = $manifestHash
        BeforeBoot = Get-BootUtc
        Utc = [DateTime]::UtcNow.ToString('o')
    }
    $bytes = [Text.Encoding]::UTF8.GetBytes(($transition | ConvertTo-Json -Compress))
    $stream = [IO.FileStream]::new($transitionFile, [IO.FileMode]::Create,
                                  [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) }
    finally { $stream.Dispose() }
    & shutdown.exe /r /t 10
    if ($LASTEXITCODE -ne 0) { throw "ordered Windows reboot rejected: $LASTEXITCODE" }
    Write-Output ($transition | ConvertTo-Json -Compress)
    return
}

if (!(Test-Path -LiteralPath $transitionFile -PathType Leaf)) { throw 'transition receipt missing' }
$transition = Get-Content -LiteralPath $transitionFile -Raw | ConvertFrom-Json
if ($transition.ManifestSha256 -ne $manifestHash) { throw 'manifest changed across reboot' }
$afterBoot = Get-BootUtc
$beforeUtc = [DateTimeOffset]::Parse($transition.BeforeBoot).UtcDateTime
$afterUtc = [DateTimeOffset]::Parse($afterBoot).UtcDateTime
$transitionUtc = [DateTimeOffset]::Parse($transition.Utc).UtcDateTime
if ($afterUtc -le $beforeUtc) { throw 'ordered reboot not observed' }
if ($transitionUtc -lt $beforeUtc) { throw 'invalid transition chronology' }
[ordered]@{ Verdict='PASS'; BeforeBoot=$transition.BeforeBoot; AfterBoot=$afterBoot;
    ManifestSha256=$manifestHash; Package=$manifest.Package.State } | ConvertTo-Json -Compress
