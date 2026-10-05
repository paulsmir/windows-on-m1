[CmdletBinding()]
param([ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Session='EXP801DxgBoot')
$ErrorActionPreference='Stop'
# Add one provider to the existing bounded experiment logger. The experiment's
# collector/rollback owns that logger and removes its complete registry tree.
$root="HKLM:\SYSTEM\CurrentControlSet\Control\WMI\Autologger\$Session"
$logger=Get-ItemProperty -LiteralPath $root
if($logger.Start -ne 1 -or $logger.LogFileMode -ne 2 -or
   $logger.MaxFileSize -le 0 -or $logger.MaxFileSize -gt 256){
    throw 'Expected staged circular experiment logger capped at 256 MiB'
}
$guid='{9e9bba3c-2e38-40cb-99f4-9e8281425164}'
$provider=Get-WinEvent -ListProvider 'Microsoft-Windows-Dwm-Core'
if([Guid]$provider.Id -ne [Guid]$guid){throw 'DWM provider identity mismatch'}
$key=Join-Path $root $guid
if(Test-Path -LiteralPath $key){throw 'DWM provider already staged'}
New-Item -Path $key -Force|Out-Null
New-ItemProperty -Path $key -Name Enabled -PropertyType DWord -Value 1|Out-Null
New-ItemProperty -Path $key -Name EnableLevel -PropertyType DWord -Value 5|Out-Null
# Microsoft AutoLogger: MatchAnyKeyword zero enables all keywords.
New-ItemProperty -Path $key -Name MatchAnyKeyword -PropertyType QWord -Value ([UInt64]0)|Out-Null
# Include the terminal session ID; no provider stack collection is requested.
New-ItemProperty -Path $key -Name EnableProperty -PropertyType DWord -Value 2|Out-Null
$actual=Get-ItemProperty -LiteralPath $key
if($actual.Enabled -ne 1 -or $actual.EnableLevel -ne 5 -or
   $actual.MatchAnyKeyword -ne 0 -or $actual.EnableProperty -ne 2){
    throw 'DWM provider registry readback mismatch'
}
[ordered]@{Utc=[DateTime]::UtcNow.ToString('o');Session=$Session;
    Name=$provider.Name;Provider=$guid;Enabled=$actual.Enabled;
    Level=$actual.EnableLevel;MatchAnyKeyword=$actual.MatchAnyKeyword;
    EnableProperty=$actual.EnableProperty;MaxFileSizeMB=$logger.MaxFileSize;
    DriverBehaviorChange=$false}|ConvertTo-Json|
    Set-Content (Join-Path $PSScriptRoot 'dwm-provider-stage.json')
Get-Content (Join-Path $PSScriptRoot 'dwm-provider-stage.json')
