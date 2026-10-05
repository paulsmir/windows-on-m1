param(
    [Parameter(Mandatory=$true)][string]$ManifestPath,
    [Parameter(Mandatory=$true)][string]$RepositoryRoot,
    [string]$SourceArchivePath,
    [switch]$SyncOnMismatch
)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
if ($manifest.repository_commit -notmatch '^[0-9a-f]{40}$' -or
    $manifest.source_scope -ne 'drivers/apple-agx/**' -or
    $manifest.file_count -ne $manifest.files.Count -or $manifest.files.Count -eq 0) {
    throw 'invalid committed source manifest'
}
$seen = @{}
$stage = $null
if ($SyncOnMismatch) {
    if ([string]::IsNullOrWhiteSpace($SourceArchivePath)) { throw 'source archive required for synchronization' }
    $archiveHash = (Get-FileHash -LiteralPath $SourceArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($archiveHash -ne $manifest.source_archive_sha256) { throw 'source archive hash mismatch' }
    $stage = Join-Path $env:TEMP ('g3-source-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage | Out-Null
    Expand-Archive -LiteralPath $SourceArchivePath -DestinationPath $stage
}
$mismatches = New-Object 'System.Collections.Generic.List[string]'
try {
    foreach ($item in $manifest.files) {
        $path = [string]$item.path
        if ($path -notmatch '^drivers/apple-agx/[A-Za-z0-9_./-]+$' -or
            $path.Split('/') -contains '..' -or
            $item.sha256 -notmatch '^[0-9a-f]{64}$' -or
            $item.git_blob_sha1 -notmatch '^[0-9a-f]{40}$' -or
            $seen.ContainsKey($path)) { throw "invalid or duplicate source path: $path" }
        $seen[$path] = $true
        $relative = $path.Replace('/', '\')
        $destination = Join-Path $RepositoryRoot $relative
        if ($stage) {
            $staged = Join-Path $stage $relative
            if (-not (Test-Path -LiteralPath $staged -PathType Leaf) -or
                (Get-FileHash -LiteralPath $staged -Algorithm SHA256).Hash.ToLowerInvariant() -ne $item.sha256) {
                throw "staged source mismatch: $path"
            }
        }
        $actual = if (Test-Path -LiteralPath $destination -PathType Leaf) {
            (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
        } else { '' }
        if ($actual -ne $item.sha256) {
            $mismatches.Add($path)
            if ($stage) {
                New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
                Copy-Item -LiteralPath $staged -Destination $destination -Force
                if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $item.sha256) {
                    throw "source synchronization failed: $path"
                }
                # Archive entries use a reproducible 1980 timestamp. Mark the
                # changed source newer than persistent MSBuild objects.
                (Get-Item -LiteralPath $destination).LastWriteTimeUtc = [datetime]::UtcNow
            }
        }
    }
    if ($mismatches.Count) {
        if ($stage) {
            throw "source mismatch: synchronized $($mismatches.Count) file(s); build refused; rerun verification"
        }
        throw "source mismatch: $($mismatches.Count) file(s); build refused"
    }
    Write-Output "source integrity PASS $($manifest.files.Count) files commit $($manifest.repository_commit)"
} finally {
    if ($stage -and (Test-Path -LiteralPath $stage)) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
