param([Parameter(Mandatory=$true)][string]$TracePath,
      [Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference='Stop'
if(!(Test-Path -LiteralPath $TracePath)){throw 'explicit original or recovery ETL path required'}
[ordered]@{TracePath=$TracePath;SHA256=(Get-FileHash -LiteralPath $TracePath).Hash;Limitation='First events only; verify native OpenTrace LogfileHeader.BootTime separately before assigning boot provenance';Events=@(Get-WinEvent -Path $TracePath -Oldest -MaxEvents 5 | ForEach-Object { [ordered]@{Time=$_.TimeCreated.ToUniversalTime().ToString('o');Provider=$_.ProviderName;Id=$_.Id;Xml=$_.ToXml()} })}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $OutputPath
