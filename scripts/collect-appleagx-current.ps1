param([string]$EvidenceDir='C:\Users\pavel\EXP488-observation')
$ErrorActionPreference='Stop'
if (Test-Path $EvidenceDir) { throw 'Evidence directory already exists' }
New-Item -ItemType Directory $EvidenceDir | Out-Null
$instance='ACPI\APPL0002\0'
$paths=[ordered]@{
    DeviceParameters='HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'
    DeviceInstance='HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0'
    Service='HKLM:\SYSTEM\CurrentControlSet\Services\AppleAgxAdmission'
}
$receiptSets=[ordered]@{}
foreach ($label in $paths.Keys) {
    $fields=[ordered]@{}
    if (Test-Path $paths[$label]) {
        $values=Get-ItemProperty $paths[$label]
        foreach ($property in $values.PSObject.Properties) {
            if ($property.Name -notlike 'Wom1*') { continue }
            if ($property.Value -is [byte[]]) {
                $file=Join-Path $EvidenceDir ($label+'-'+$property.Name+'.bin')
                [IO.File]::WriteAllBytes($file,$property.Value)
                $fields[$property.Name]=@{Bytes=$property.Value.Length;SHA256=(Get-FileHash $file).Hash;File=[IO.Path]::GetFileName($file)}
            } else { $fields[$property.Name]=$property.Value }
        }
    }
    $receiptSets[$label]=$fields
}
$properties=[ordered]@{}
foreach ($name in 'DEVPKEY_Device_DriverInfPath','DEVPKEY_Device_Service','DEVPKEY_Device_ProblemCode','DEVPKEY_Device_ProblemStatus','DEVPKEY_Gpu_Luid') {
    $properties[$name]=(Get-PnpDeviceProperty -InstanceId $instance -KeyName $name -ErrorAction SilentlyContinue).Data
}
$boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime
$result=[ordered]@{
    Utc=[DateTime]::UtcNow.ToString('o');Boot=$boot
    Receipts=$receiptSets;Properties=$properties
    Cpus=(Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
    Pnp=@(Get-PnpDevice -InstanceId $instance -ErrorAction SilentlyContinue | Select-Object Status,Problem,InstanceId)
    Packages=@(Get-WindowsDriver -Online | Where-Object {$_.OriginalFileName -match 'appleagx' -or $_.ProviderName -match 'Apple Silicon|Pavel Build Lab'} | Select-Object Driver,OriginalFileName,ProviderName,Version)
    Drivers=@(Get-CimInstance Win32_SystemDriver | Where-Object {$_.Name -match 'Agx|stornvme|USBXHCI|AppleInput'} | Select-Object Name,State,PathName)
    BootExecute=(Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager').BootExecute
    Events=@(Get-WinEvent -FilterHashtable @{LogName='System';StartTime=$boot;Id=41,129,1001} -ErrorAction SilentlyContinue | Select-Object TimeCreated,RecordId,Id,ProviderName,Message)
}
$result | ConvertTo-Json -Depth 9 | Set-Content "$EvidenceDir\result.json" -Encoding UTF8
Get-Content "$EvidenceDir\result.json"
