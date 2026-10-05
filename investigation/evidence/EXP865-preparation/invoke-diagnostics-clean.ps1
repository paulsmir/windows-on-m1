param([Parameter(Mandatory=$true)][ValidatePattern('^[0-9A-Fa-f]{64}$')][string]$ExpectedEvidenceSha256)
& 'C:\Users\pavel\EXP865\invoke-hidden-diagnostics-clean.ps1' -ExpectedEvidenceSha256 $ExpectedEvidenceSha256
