# Probe protocol (EXP1097+): Notepad restores its previous session, so every run opened
# more windows than the last. Close Notepad and move (not delete) its saved window/tab
# state to an archive, so the load always starts from one fresh window.
Get-Process notepad -EA 0 | Stop-Process -Force; Start-Sleep 1
$p = Get-ChildItem $env:LOCALAPPDATA\Packages -Directory -Filter Microsoft.WindowsNotepad* | Select -First 1
$dst = "$env:USERPROFILE\notepad-session-archive\" + (Get-Date -Format 'yyyyMMdd-HHmmss')
New-Item -ItemType Directory -Force $dst | Out-Null
$n = 0
foreach ($d in 'TabState','WindowState') {
  $src = Join-Path $p.FullName "LocalState\$d"
  if (Test-Path $src) { New-Item -ItemType Directory -Force (Join-Path $dst $d) | Out-Null
    Get-ChildItem $src -File | % { Move-Item $_.FullName (Join-Path $dst $d) -Force; $n++ } }
}
"np-reset moved $n session files to $dst"
