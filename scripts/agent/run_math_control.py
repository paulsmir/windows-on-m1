#!/usr/bin/env python3
"""Run exactly the reviewed Phase 3 math validation; no caller shell text."""
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import uuid

COMMAND = "RUN_MATH_CONTROL"
EXPECTED_SHA256 = "c1a5d554c941a78c3fa02e77c370e8cbb0e5ecb8291177f277f079a07cd67d13"
APPROVED = pathlib.Path("/Users/pavel/public_windows/.worktrees/task-AD04-MATH-CONSTANTS-006/drivers/apple-agx/mesa/scripts/run-phase3-normalized.ps1")
LOCAL_ROOT = pathlib.Path("/Users/pavel/public_windows/.worktrees/phase3-normalize")
KEY = "/Users/pavel/.ssh/windows_builder"
HOST = "pauls@192.168.1.24"
INPUT_ROOT = "C:/Users/pauls/AD04-phase3-normalize-20260912T000001Z/input"


def call(arguments):
    return subprocess.run(arguments, check=True, text=True, capture_output=True)


def main():
    if len(sys.argv) != 2 or sys.argv[1] != COMMAND:
        raise SystemExit("only RUN_MATH_CONTROL is accepted")
    actual = hashlib.sha256(APPROVED.read_bytes()).hexdigest()
    if actual != EXPECTED_SHA256:
        raise SystemExit(f"approved script hash mismatch: {actual}")
    token = uuid.uuid4().hex
    remote = f"C:/Users/pauls/AD04-phase3-math-{token}"
    local = LOCAL_ROOT / "investigation/evidence/PHASE3-math" / token
    local.mkdir(parents=True, exist_ok=False)
    ssh = ["ssh", "-i", KEY, "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=yes", HOST]
    scp = ["scp", "-i", KEY, "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=yes"]
    preparer = LOCAL_ROOT / "drivers/apple-agx/mesa/scripts/prepare-phase3-normalized.ps1"
    probe = LOCAL_ROOT / "drivers/apple-agx/mesa/scripts/run-math-constants-probe.ps1"
    probe_c = LOCAL_ROOT / "drivers/apple-agx/mesa/windows-overlay/math_constants_probe.c"
    call(scp + [str(preparer), f"{HOST}:C:/Users/pauls/phase3-math-preparer-{token}.ps1"])
    call(ssh + [f"powershell -NoProfile -ExecutionPolicy Bypass -File C:/Users/pauls/phase3-math-preparer-{token}.ps1 -Root {remote}"])
    call(scp + [str(APPROVED), str(probe), str(probe_c), f"{HOST}:{remote}/"])
    probe_result = call(ssh + [f"powershell -NoProfile -ExecutionPolicy Bypass -File {remote}/run-math-constants-probe.ps1 -Root {remote}/probe"])
    compile_result = subprocess.run(ssh + [f"powershell -NoProfile -ExecutionPolicy Bypass -File {remote}/run-phase3-normalized.ps1 -InputRoot {INPUT_ROOT} -ResultRoot {remote}/compile"], text=True, capture_output=True)
    call(scp + ["-r", f"{HOST}:{remote}/probe/.", str(local / "probe")])
    call(scp + ["-r", f"{HOST}:{remote}/compile/.", str(local / "compile")])
    manifest = {"command": COMMAND, "approved_sha256": actual, "remote_root": remote,
                "input_root": INPUT_ROOT, "probe_ssh": probe_result.returncode,
                "compile_ssh": compile_result.returncode, "compile_stdout": compile_result.stdout,
                "compile_stderr": compile_result.stderr}
    (local / "runner-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if compile_result.returncode != 0:
        raise SystemExit(compile_result.returncode)


if __name__ == "__main__":
    main()
