# Re-catalog an already built, signed render-admission package with a new INF
# (and the x86 OpenGL ICD) without rebuilding the binaries.
#
# EXP1156-EXP1159: dxgkrnl fails DpiAddDevice with STATUS_DEVICE_CONFIGURATION_ERROR
# (Code 31) when the INF DriverVer differs from the KMD VERSIONINFO
# (CHG-20260904-461/462). The DriverVer is therefore taken from the KMD
# binary, never from the caller, and verify-package-version.ps1 gates the
# result exactly as build-driver.ps1 does.
param(
    [Parameter(Mandatory = $true)][string]$BinaryDirectory,
    [Parameter(Mandatory = $true)][string]$Inf,
    [Parameter(Mandatory = $true)][string]$Out,
    [string]$WowOpenGLIcd,
    [Parameter(Mandatory = $true)][string]$SignerThumbprint,
    [string]$Kit = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0',
    [string]$SignTool = 'C:\Users\pauls\packages\Microsoft.Windows.SDK.CPP.10.0.28000.2526\c\bin\10.0.28000.0\x86\signtool.exe'
)
$ErrorActionPreference = 'Stop'
$sys = Join-Path $BinaryDirectory 'AppleAgxRenderAdmission.sys'
$umd = Join-Path $BinaryDirectory 'AppleAgxRenderAdmissionUmd.dll'
$info = [Diagnostics.FileVersionInfo]::GetVersionInfo($sys)
if ([string]::IsNullOrWhiteSpace($info.FileVersion)) { throw 'KMD has no VERSIONINFO' }
$version = '{0}.{1}.{2}.{3}' -f $info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart, $info.FilePrivatePart
New-Item -ItemType Directory -Force $Out | Out-Null
Get-ChildItem $Out -File | Remove-Item -Force
Copy-Item $sys, $umd $Out
Copy-Item $Inf (Join-Path $Out 'AppleAgxRenderAdmission.inf')
if ($WowOpenGLIcd) { Copy-Item $WowOpenGLIcd (Join-Path $Out 'AppleAgxOpenGL32.dll') }
# Native tools report through stderr and exit codes; judge the exit codes.
$ErrorActionPreference = 'Continue'
& "$Kit\x64\stampinf.exe" -d '*' -a 'ARM64' -v $version -k '1.15' -x -f (Join-Path $Out 'AppleAgxRenderAdmission.inf')
if ($LASTEXITCODE) { throw "stampinf failed $LASTEXITCODE" }
& "$Kit\x86\inf2cat.exe" /os:Server10_arm64 /USELOCALTIME "/driver:$Out\"
if ($LASTEXITCODE) { throw "Inf2Cat failed $LASTEXITCODE" }
& $SignTool sign /ph /fd sha256 /sha1 $SignerThumbprint (Join-Path $Out 'appleagxrenderadmission.cat')
if ($LASTEXITCODE) { throw "signtool failed $LASTEXITCODE" }
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'verify-package-version.ps1') -PackageDirectory $Out
