param(
    [Parameter(Mandatory=$true)][string]$SourceManifestPath,
    [Parameter(Mandatory=$true)][string]$SourceArchivePath,
    [Parameter(Mandatory=$true)][string]$PackageManifestPath,
    [Parameter(Mandatory=$true)][string]$BuildLogPath,
    [Parameter(Mandatory=$true)][int]$PackageBuild,
    [switch]$VerifyOnly
)
$ErrorActionPreference = 'Stop'
$repository = 'C:\Users\pauls\AD04-persistent-dwm-next'
$driver = Join-Path $repository 'drivers\apple-agx\render-admission'
$verify = Join-Path $driver 'scripts\verify-committed-sources.ps1'
& $verify -ManifestPath $SourceManifestPath -RepositoryRoot $repository `
    -SourceArchivePath $SourceArchivePath -SyncOnMismatch
if ($VerifyOnly) { return }

$build = Join-Path $driver 'scripts\build-driver.ps1'
& $build -Configuration Release -GpuvaG3Qualification -UmdAdmissionTrace `
    -NativeFrontend -NativeRuntimeProps 'C:\Users\pauls\AD04-fullcompiler-001\asahi-runtime-arm64-dwm-next-verified-arm64\NativeRuntime.props' `
    -MesaSourceRoot 'C:\Users\pauls\AD04-d3d10-frontend-build\mesa' `
    -MesaGeneratedRoot 'C:\Users\pauls\AD04-asahi-windows-compiler\b5\generated' `
    -PackageBuild $PackageBuild -GpuvaG1bPageProfile 16 `
    -SourceManifestPath $SourceManifestPath *> $BuildLogPath
if ($LASTEXITCODE) { throw "build-driver exit $LASTEXITCODE" }

$source = Get-Content -LiteralPath $SourceManifestPath -Raw | ConvertFrom-Json
$package = Join-Path $driver 'ARM64\Release\AppleAgxRenderAdmission'
$artifacts = @{}
foreach ($name in @('AppleAgxRenderAdmission.inf', 'AppleAgxRenderAdmission.sys',
                   'AppleAgxRenderAdmissionUmd.dll', 'AppleAgxRenderAdmission.cat')) {
    $path = Join-Path $package $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "package artifact missing: $name" }
    $artifacts[$name] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}
$output = [ordered]@{
    package_build = $PackageBuild
    repository_commit = $source.repository_commit
    source_archive_sha256 = $source.source_archive_sha256
    source_file_count = $source.file_count
    abi_headers = $source.abi_headers
    source_hashes = $source.files
    artifact_sha256 = $artifacts
}
$output | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $PackageManifestPath -Encoding UTF8
Write-Output "verified package $PackageBuild sources=$($source.file_count) manifest=$PackageManifestPath"
