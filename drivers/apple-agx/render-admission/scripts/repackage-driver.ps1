# Re-catalog an already built, signed render-admission package with a new INF
# (and the x86 OpenGL ICD and x86 D3D UMD) without rebuilding the binaries.
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
    # EXP1222: the WOW64 D3D UMD (build-umd-x86.ps1), UserModeDriverNameWow.
    [string]$WowUmd,
    # EXP1820: x64 processes on ARM64 Windows load the same System32 UMD name
    # (EXP1800). An ARM64X forwarder under that name sends native callers to
    # AppleAgxRenderAdmissionUmdArm64.dll and x64 callers to
    # AppleAgxRenderAdmissionUmdX64.dll (EXP1805); pass both or neither.
    [string]$X64Umd,
    [string]$Arm64xForwarder,
    [Parameter(Mandatory = $true)][string]$SignerThumbprint,
    [string]$Kit = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0',
    [string]$SignTool = ''
)
$builderRoot = if ($env:APPLE_AGX_BUILDER_ROOT) { $env:APPLE_AGX_BUILDER_ROOT } else { 'C:\Users\pauls\APB12E~1\active' }  # builder root: APPLE AGX\active (8.3 alias, no space)
if (!$SignTool) { $SignTool = "$builderRoot\packages\Microsoft.Windows.SDK.CPP.10.0.28000.2526\c\bin\10.0.28000.0\x86\signtool.exe" }
$ErrorActionPreference = 'Stop'
$sys = Join-Path $BinaryDirectory 'AppleAgxRenderAdmission.sys'
$umd = Join-Path $BinaryDirectory 'AppleAgxRenderAdmissionUmd.dll'
$info = [Diagnostics.FileVersionInfo]::GetVersionInfo($sys)
if ([string]::IsNullOrWhiteSpace($info.FileVersion)) { throw 'KMD has no VERSIONINFO' }
$version = '{0}.{1}.{2}.{3}' -f $info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart, $info.FilePrivatePart
New-Item -ItemType Directory -Force $Out | Out-Null
Get-ChildItem $Out -File | Remove-Item -Force
if ($X64Umd -or $Arm64xForwarder) {
    if (-not ($X64Umd -and $Arm64xForwarder)) { throw 'pass both -X64Umd and -Arm64xForwarder' }
    Copy-Item $sys $Out
    Copy-Item $umd (Join-Path $Out 'AppleAgxRenderAdmissionUmdArm64.dll')
    Copy-Item $Arm64xForwarder (Join-Path $Out 'AppleAgxRenderAdmissionUmd.dll')
    Copy-Item $X64Umd (Join-Path $Out 'AppleAgxRenderAdmissionUmdX64.dll')
} else {
    Copy-Item $sys, $umd $Out
}
Copy-Item $Inf (Join-Path $Out 'AppleAgxRenderAdmission.inf')
if ($WowOpenGLIcd) { Copy-Item $WowOpenGLIcd (Join-Path $Out 'AppleAgxOpenGL32.dll') }
if ($WowUmd) { Copy-Item $WowUmd (Join-Path $Out 'AppleAgxRenderAdmissionUmd32.dll') }
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
