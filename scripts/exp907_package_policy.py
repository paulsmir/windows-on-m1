"""Apply EXP907 diagnostic and disk gates before experiment manifests are sealed."""

from pathlib import Path
import hashlib
import re
import shutil


POLICY = Path('/Users/pavel/public_windows/.local/experiments/EXP907-disk/staged-disk-policy.ps1')


def replace_once(path, before, after):
    source = path.read_text(encoding='utf-8-sig')
    if source.count(before) != 1:
        raise ValueError(f"expected exactly one policy site in {path.name}: {before[:60]}")
    path.write_text(source.replace(before, after))


def apply(base, experiment_number, worktree):
    if experiment_number != '907':
        raise ValueError('EXP907 policy cannot be reused without review')
    base = Path(base)
    gate = Path(worktree) / 'scripts/verify_guest_disk_launch.py'
    if not POLICY.is_file() or not gate.is_file():
        raise FileNotFoundError('disk policy or host gate absent')
    shutil.copy2(POLICY, base / 'staged-disk-policy.ps1')
    shutil.copy2(gate, base / 'verify_guest_disk_launch.py')
    policy_sha = hashlib.sha256(POLICY.read_bytes()).hexdigest()

    autologger = base / 'autologger-stage.ps1'
    replace_once(autologger, '-Name LogFileMode -PropertyType DWord -Value 1',
                 '-Name LogFileMode -PropertyType DWord -Value 2')
    replace_once(autologger, '-Name MaxFileSize -PropertyType DWord -Value 512',
                 '-Name MaxFileSize -PropertyType DWord -Value 256')

    stage = base / 'stage.ps1'
    replace_once(stage, '-Name DumpType -PropertyType DWord -Value 2',
                 '-Name DumpType -PropertyType DWord -Value 1')
    replace_once(stage, '-Name DumpCount -PropertyType DWord -Value 5',
                 '-Name DumpCount -PropertyType DWord -Value 1')
    replace_once(stage, '$w.DumpType -ne 2 -or $w.DumpCount -ne 5',
                 '$w.DumpType -ne 1 -or $w.DumpCount -ne 1')
    stage_source = stage.read_text()
    receipt_pattern = r'(?m)^\$receipt=\[ordered\]@\{.*\}$'
    new_receipt = (
        "$preflight=Get-Content (Join-Path $base 'guest-preflight.json') -Raw|ConvertFrom-Json\n"
        "$receipt=[ordered]@{Utc=[DateTime]::UtcNow.ToString('o');Experiment='EXP907';"
        "Boot=$preflight.Boot;Inf=$staged[0].Driver;"
        "InfHash=$expected['AppleAgxRenderAdmission.inf'];"
        "PackageVersion='30.0.907.0';Arm=1;OrderedShutdown='PowerOff';"
        "NextProfile='cold-full-owner';PreflightSha256=(Get-FileHash "
        "(Join-Path $base 'guest-preflight.json')).Hash.ToLowerInvariant();"
        "ManifestSha256=(Get-FileHash (Join-Path $base 'hardware-manifest.json')).Hash.ToLowerInvariant()}"
    )
    stage_source, count = re.subn(receipt_pattern, lambda _: new_receipt, stage_source)
    if count != 1:
        raise ValueError('stage receipt shape changed')
    insertion = (
        "$policy=Join-Path $base 'staged-disk-policy.ps1'\n"
        f"if((Get-FileHash $policy).Hash.ToLowerInvariant() -ne '{policy_sha}')"
        "{throw 'staged disk policy hash mismatch'}\n"
        "& $policy\nif(!$?){throw 'staged disk policy refused'}\n"
    )
    if stage_source.count('$preflight=Get-Content') != 1:
        raise ValueError('stage preflight insertion point missing')
    stage_source = stage_source.replace('$preflight=Get-Content',
                                        insertion + '$preflight=Get-Content', 1)
    if stage_source.count('shutdown.exe /s /t 10') != 1:
        raise ValueError('stage shutdown timer changed')
    stage.write_text(stage_source.replace('shutdown.exe /s /t 10',
                                          'shutdown.exe /s /t 30'))

    for name in ('diagnostics-clean.ps1', 'invoke-hidden-diagnostics-clean.ps1',
                 'invoke-verified-hidden-diagnostics-clean.ps1'):
        path = base / name
        replace_once(path, '$w.DumpType -ne 2 -or $w.DumpCount -ne 5',
                     '$w.DumpType -ne 1 -or $w.DumpCount -ne 1')

    launcher = base / 'full-owner-direct.sh'
    replace_once(launcher,
                 'if [ "${AIR_SERIAL_LOCK_ACTIVE:-}" != 1 ]; then',
                 'python3 "$base/verify_guest_disk_launch.py" '
                 '"$base/readiness-manifest.json" "$base/guest-preflight.json" '
                 '"$base/stage-receipt.json"\n'
                 'if [ "${AIR_SERIAL_LOCK_ACTIVE:-}" != 1 ]; then')

    sample = base / 'sample.ps1'
    replace_once(sample,
                 '[ordered]@{Utc=[DateTime]::UtcNow.ToString(\'o\');',
                 "$werBytes=0L;foreach($file in Get-ChildItem 'C:\\CrashDumps' -File "
                 "-ErrorAction SilentlyContinue){$werBytes+=$file.Length}\n"
                 "[ordered]@{Utc=[DateTime]::UtcNow.ToString('o');")
    replace_once(sample, 'Cpu=(Get-CimInstance Win32_ComputerSystem)',
                 'FreeC=(Get-PSDrive C).Free;WerFolderBytes=$werBytes;'
                 'Cpu=(Get-CimInstance Win32_ComputerSystem)')
    monitor = base / 'monitor.py'
    replace_once(monitor, "s['ElapsedBootSeconds']=(now-boot).total_seconds()",
                 "s['ElapsedBootSeconds']=(now-boot).total_seconds()\n"
                 "   if s.get('FreeC',0)<4*1024**3 or "
                 "s.get('WerFolderBytes',134217729)>134217728:\n"
                 "    f.write(json.dumps({'DiskBudgetBreach':s})+'\\n');"
                 "print('DISK_BUDGET_BREACH',flush=True);raise SystemExit(5)")

    periodic = base / 'periodic-original-copy.py'
    cleanup = '''    cleanup_ps = r"""
$ErrorActionPreference='Stop'
$root='C:\\Users\\pavel\\EXP907-live-snapshot\\TAG'
$receipt=Get-Content (Join-Path $root 'snapshot-receipt.json') -Raw|ConvertFrom-Json
$files=@(Get-ChildItem $root -File)
if($files.Count -ne $receipt.Files.Count+1){throw 'snapshot cleanup file count'}
foreach($item in $receipt.Files){
 $path=Join-Path $root $item.Name
 $file=Get-Item -LiteralPath $path
 if($file.Length -ne $item.Bytes -or (Get-FileHash $path).Hash.ToLowerInvariant() -ne $item.SHA256){throw 'snapshot cleanup hash'}
}
Remove-Item -LiteralPath $root -Recurse -Force
if(Test-Path $root){throw 'snapshot cleanup residue'}
'SNAPSHOT_GUEST_DUPLICATE_REMOVED'
""".replace('TAG', tag)
    encoded_cleanup = base64.b64encode(cleanup_ps.encode('utf-16le')).decode()
    cleaned = subprocess.run(SSH + ['powershell.exe -NoProfile -NonInteractive '
        '-ExecutionPolicy Bypass -EncodedCommand ' + encoded_cleanup],
        capture_output=True, timeout=90)
    if cleaned.returncode or b'SNAPSHOT_GUEST_DUPLICATE_REMOVED' not in cleaned.stdout:
        raise RuntimeError('GUEST_CLEANUP_FAILED: ' + cleaned.stderr.decode(errors='replace')[-300:])
'''
    replace_once(periodic,
                 "    return {'Tag': tag, 'Boot': got['BootBefore']",
                 cleanup + "    return {'Tag': tag, 'Boot': got['BootBefore']")
    replace_once(periodic,
                 "                if 'BOOT_CHANGED' in str(exc):",
                 "                if 'GUEST_CLEANUP_FAILED' in str(exc): return 5\n"
                 "                if 'BOOT_CHANGED' in str(exc):")

    audit = base / 'audit_hash_literals.py'
    replace_once(audit,
                 '"stage.ps1": PKG + ["pinned.cer", "../EXP726-air-signing/signtool-arm64.exe"],',
                 '"stage.ps1": PKG + ["pinned.cer", "../EXP726-air-signing/signtool-arm64.exe", "staged-disk-policy.ps1"],')
    replace_once(audit,
                 '"payload_sha256": "guest-payload.tar",',
                 '"payload_sha256": "guest-payload.tar",\n'
                 '    "launch_gate_sha256": "verify_guest_disk_launch.py",')
    for name in ('AppleAgxBltProbe.exe', 'query-frame.ps1', 'poll-frame.py'):
        if not (base / name).is_file():
            raise FileNotFoundError(name)
    replace_once(monitor, "    (b/'periodic-copy.pid').write_text(str(periodic.pid)+'\\n')",
                 "    (b/'periodic-copy.pid').write_text(str(periodic.pid)+'\\n')\n"
                 "    with (b/'frame-poll-launch.log').open('a') as frame_log:\n"
                 "     frame_poll=subprocess.Popen([sys.executable,str(b/'poll-frame.py'),first],stdin=subprocess.DEVNULL,stdout=frame_log,stderr=subprocess.STDOUT,start_new_session=True)\n"
                 "    (b/'frame-poll.pid').write_text(str(frame_poll.pid)+'\\n')")
    return ['staged-disk-policy.ps1', 'verify_guest_disk_launch.py',
            'AppleAgxBltProbe.exe', 'query-frame.ps1', 'poll-frame.py']
