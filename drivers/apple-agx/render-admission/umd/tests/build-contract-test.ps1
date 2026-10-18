param(
    [string]$Configuration = "Release",
    [ValidateSet("x64", "ARM64")]
    [string]$Platform = "x64",
    # Build, log and run output land here; defaults to the project directory.
    [string]$ResultRoot = ""
)

$ErrorActionPreference = "Stop"
$project = Join-Path $PSScriptRoot "UmdContractTest.vcxproj"
if ([string]::IsNullOrWhiteSpace($ResultRoot)) {
    $ResultRoot = Join-Path $PSScriptRoot "$Platform\$Configuration"
}
[void](New-Item -ItemType Directory -Force $ResultRoot)
$ResultRoot = (Resolve-Path $ResultRoot).Path
$buildLog = Join-Path $ResultRoot "build.log"
$runOut = Join-Path $ResultRoot "run-stdout.log"
$runErr = Join-Path $ResultRoot "run-stderr.log"

# Same MSBuild selection as scripts/build-driver.ps1 (no -prerelease), so the
# contract test compiles with the toolchain that builds the shipped UMD.
$msbuildCommand = Get-Command msbuild -ErrorAction SilentlyContinue
if ($null -ne $msbuildCommand) {
    $msbuild = $msbuildCommand.Source
} else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "MSBuild is not on PATH and vswhere.exe was not found"
    }
    $msbuild = & $vswhere -latest -products * `
        -requires Microsoft.Component.MSBuild `
        -find "MSBuild\**\Bin\amd64\MSBuild.exe" | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($msbuild)) {
        throw "A Visual Studio installation with x64 MSBuild support was not found"
    }
}
Write-Output "MSBuild: $msbuild"

$outDir = "$ResultRoot\"
& $msbuild $project /m:2 /nr:false /nologo /v:m /t:Clean,Build `
    "/p:Configuration=$Configuration" "/p:Platform=$Platform" `
    "/p:OutDir=$outDir" "/p:IntDir=$ResultRoot\obj\" `
    "/flp:LogFile=$buildLog;Verbosity=normal"
$buildExit = $LASTEXITCODE
$binary = Join-Path $ResultRoot "UmdContractTest.exe"
$result = [ordered]@{
    Configuration = $Configuration
    Platform = $Platform
    BuildExitCode = $buildExit
    Binary = $null
    SHA256 = $null
    ExitCode = $null
    ExitStatus = $null
    StderrLines = $null
    CheckFailures = $null
    Result = "FAIL"
    ResultRoot = $ResultRoot
}
if ($buildExit -eq 0 -and (Test-Path $binary)) {
    $result.Binary = $binary
    $result.SHA256 = (Get-FileHash $binary -Algorithm SHA256).Hash
    # Start-Process keeps the test's stderr out of the PowerShell error stream:
    # with ErrorActionPreference=Stop a redirected native stderr line would
    # otherwise abort this script after the first CHECK_FAIL.
    $process = Start-Process -FilePath $binary -WorkingDirectory $ResultRoot `
        -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $runOut -RedirectStandardError $runErr
    $result.ExitCode = $process.ExitCode
    # A crash exits with its NTSTATUS (0xC0000005 access violation,
    # 0xC00000FD stack overflow); a clean exit returns the failure count.
    $result.ExitStatus = "0x{0:X8}" -f $process.ExitCode
    $stderrLines = @(Get-Content $runErr)
    $result.StderrLines = $stderrLines.Count
    $failures = @(Select-String -Path $runOut, $runErr -Pattern "CHECK_FAIL" -SimpleMatch)
    $result.CheckFailures = $failures.Count
    Get-Content $runOut, $runErr
    if ($process.ExitCode -eq 0 -and $stderrLines.Count -eq 0) {
        $result.Result = "PASS"
    }
} else {
    Select-String -Path $buildLog -Pattern ": (fatal )?error " |
        ForEach-Object { $_.Line.Trim() } | Sort-Object -Unique
}
$result | ConvertTo-Json
if ($result.Result -ne "PASS") {
    exit 1
}
exit 0
