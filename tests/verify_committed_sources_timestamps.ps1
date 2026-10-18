param([Parameter(Mandatory=$true)][string]$VerifyScript)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$temp = Join-Path $env:TEMP ('r83-timestamp-' + [guid]::NewGuid().ToString('N'))
$root = Join-Path $temp 'repository'
$stage = Join-Path $temp 'stage'
$relative = 'drivers/apple-agx/r83-timestamp-fixture.c'
$destination = Join-Path $root ($relative.Replace('/', '\'))
$staged = Join-Path $stage ($relative.Replace('/', '\'))
$archive = Join-Path $temp 'source.zip'
$manifestPath = Join-Path $temp 'manifest.json'
try {
  New-Item -ItemType Directory -Path (Split-Path -Parent $destination), (Split-Path -Parent $staged) -Force | Out-Null
  Set-Content -LiteralPath $destination -Value 'old source' -NoNewline
  Set-Content -LiteralPath $staged -Value 'new source' -NoNewline
  (Get-Item -LiteralPath $staged).LastWriteTimeUtc = [datetime]'1980-01-01T00:00:00Z'
  Compress-Archive -Path (Join-Path $stage 'drivers') -DestinationPath $archive
  $sourceHash = (Get-FileHash -LiteralPath $staged -Algorithm SHA256).Hash.ToLowerInvariant()
  $archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
  $manifest = [ordered]@{
    repository_commit = ('a' * 40)
    source_scope = 'drivers/apple-agx/**'
    file_count = 1
    files = @([ordered]@{ path = $relative; sha256 = $sourceHash; git_blob_sha1 = ('b' * 40) })
    source_archive_sha256 = $archiveHash
  }
  $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
  $stopped = $false
  try {
    & $VerifyScript -ManifestPath $manifestPath -RepositoryRoot $root -SourceArchivePath $archive -SyncOnMismatch
  } catch {
    if ($_.Exception.Message -match 'source mismatch: synchronized 1 file') { $stopped = $true }
    else { throw }
  }
  if (-not $stopped) { throw 'R83 did not refuse the first mismatched build' }
  if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sourceHash) {
    throw 'R83 did not copy the hash-verified source'
  }
  if ((Get-Item -LiteralPath $destination).LastWriteTimeUtc -lt [datetime]::UtcNow.AddMinutes(-2)) {
    throw 'R83 synchronized source retained an old archive timestamp'
  }
  & $VerifyScript -ManifestPath $manifestPath -RepositoryRoot $root
  Write-Output 'R83_TIMESTAMP_REBUILD_PASS'
} finally {
  if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
}
