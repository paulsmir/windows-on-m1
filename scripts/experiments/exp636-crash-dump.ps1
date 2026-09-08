param([ValidateSet('Configure','Restore','Inspect')][string]$Mode='Inspect')
$ErrorActionPreference='Stop'
$key='HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl'
$backup='C:\Users\pavel\EXP636-crashcontrol-before.json'
if($Mode-eq'Configure'){
  if(Test-Path $backup){throw 'backup exists; inspect before any repeat'}
  $value=Get-ItemPropertyValue -LiteralPath $key -Name CrashDumpEnabled
  if($value-ne3){throw 'expected original small-dump setting3'}
  if(Test-Path C:\Windows\MEMORY.DMP){throw 'preserve existing MEMORY.DMP first'}
  if(-not(Get-CimInstance Win32_ComputerSystem).AutomaticManagedPagefile){throw 'pagefile not system managed'}
  $before=[ordered]@{Utc=(Get-Date).ToUniversalTime().ToString('o');CrashDumpEnabled=[int]$value;DumpFile=(Get-ItemPropertyValue -LiteralPath $key -Name DumpFile);Pagefile=@(Get-CimInstance Win32_PageFileUsage|Select-Object Name,AllocatedBaseSize)}
  $before|ConvertTo-Json -Depth 4|Set-Content $backup -Encoding UTF8
  Set-ItemProperty -LiteralPath $key -Name CrashDumpEnabled -Type DWord -Value 7
  if((Get-ItemPropertyValue -LiteralPath $key -Name CrashDumpEnabled)-ne7){throw 'configure readback failed'}
}
if($Mode-eq'Restore'){
  $before=Get-Content $backup -Raw|ConvertFrom-Json
  if($before.CrashDumpEnabled-ne3-or(Get-ItemPropertyValue -LiteralPath $key -Name CrashDumpEnabled)-ne7){throw 'restore state mismatch'}
  Set-ItemProperty -LiteralPath $key -Name CrashDumpEnabled -Type DWord -Value ([int]$before.CrashDumpEnabled)
}
Get-ItemProperty -LiteralPath $key|Select-Object CrashDumpEnabled,DumpFile,AutoReboot,LogEvent|ConvertTo-Json
