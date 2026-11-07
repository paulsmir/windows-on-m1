# Run only the actual recovery helper's path resolver against deterministic
# strings. No service, package or registry mutation.
[CmdletBinding()]
param([string]$HelperPath = (Join-Path $PSScriptRoot '..\scripts\agx-recovery-disable-service.ps1'))
$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path -LiteralPath $HelperPath).Path,[ref]$tokens,[ref]$errors)
if(@($errors).Count){throw 'helper parse failure'}
$f=@($ast.FindAll({param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Resolve-ExactDriverStoreSysPath'},$true))
if($f.Count -ne 1){throw 'missing exact DriverStore resolver'}
. ([scriptblock]::Create($f[0].Extent.Text))
$root='C:\Windows'
$dir='C:\Windows\System32\DriverStore\FileRepository\appleagxrenderadmission.inf_arm64_6f5750a0783c3fd6'
$inf=Join-Path $dir 'appleagxrenderadmission.inf'
$sys=Join-Path $dir 'AppleAgxRenderAdmission.sys'
$image='\SystemRoot\System32\DriverStore\FileRepository\appleagxrenderadmission.inf_arm64_6f5750a0783c3fd6\AppleAgxRenderAdmission.sys'
if((Resolve-ExactDriverStoreSysPath $inf $image $root) -ine $sys){throw 'DriverStore case not resolved'}
Write-Output 'RECOVERY_IMAGEPATH DriverStore PASS'
foreach($case in @(
  @($inf,'\SystemRoot\System32\drivers\AppleAgxRenderAdmission.sys',$root),
  @($inf,'\SystemRoot\System32\DriverStore\FileRepository\other.inf_arm64\AppleAgxRenderAdmission.sys',$root),
  @((Join-Path $dir 'other.inf'),$image,$root),
  @('C:\Temp\appleagxrenderadmission.inf',$image,$root)
)){
 $blocked=$false
 try{$null=Resolve-ExactDriverStoreSysPath $case[0] $case[1] $case[2]}catch{$blocked=$true}
 if(!$blocked){throw 'incorrect image path accepted'}
}
Write-Output 'RECOVERY_IMAGEPATH mismatches REFUSED'
Write-Output 'AGX_RECOVERY_IMAGEPATH_GATE: PASS'
