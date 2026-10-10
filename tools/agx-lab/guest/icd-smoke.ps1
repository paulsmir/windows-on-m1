# Run the x86 GL smoke test (agx_gl_smoke.exe) with the private AGX ICD in the
# interactive session; start it with arm-interactive.ps1. The ICD files, the
# smoke test and this script share one directory. Modes: '' (colour + depth
# window), 'nodepth', 'fbo', 'depthtest'.
param([int]$Frames = 300, [string]$Mode = '')
$d = $PSScriptRoot
Remove-Item "$d\smoke-out.txt", "$d\smoke-err.txt", "$d\smoke-done.txt", "$d\icd-umd.log", "$d\agx_gl_smoke.dmp", "$d\agx_gl_smoke_hang.dmp", "$d\agx_gl_smoke_abort.dmp" -EA 0
$env:APPLE_AGX_UMD_TRACE_FILE = "$d\icd-umd.log"
$smokeArgs = @("$Frames")
if ($Mode) { $smokeArgs += @('-', $Mode) }
$t0 = Get-Date
$p = Start-Process -FilePath "$d\agx_gl_smoke.exe" -ArgumentList $smokeArgs -WorkingDirectory $d -RedirectStandardOutput "$d\smoke-out.txt" -RedirectStandardError "$d\smoke-err.txt" -PassThru
if (-not $p.WaitForExit(90000)) { try { $p.Kill() } catch {}; $r = 'TIMEOUT' } else { $r = 'exit ' + $p.ExitCode }
"$r seconds " + [int]((Get-Date) - $t0).TotalSeconds | Out-File "$d\smoke-done.txt"
