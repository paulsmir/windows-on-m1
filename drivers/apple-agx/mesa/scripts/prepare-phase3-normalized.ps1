param([Parameter(Mandatory = $true)][string]$Root)
$ErrorActionPreference = 'Stop'
if (Test-Path $Root) { throw "refusing to reuse existing root: $Root" }
New-Item -ItemType Directory -Force (Join-Path $Root 'input') | Out-Null
