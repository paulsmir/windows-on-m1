#!/usr/bin/env python3
"""Run only registry-defined Tier C commands authorized by a JSON task contract."""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def scope(repo, input_commit, allowed):
    tracked = git(repo, "diff", "--name-only", input_commit, "--").stdout.splitlines()
    untracked = git(repo, "ls-files", "--others", "--exclude-standard").stdout.splitlines()
    changed = sorted(set(tracked + untracked))
    forbidden = [p for p in changed if not any(p == r or (r.endswith("/") and p.startswith(r)) for r in allowed)]
    return {"changed_paths": changed, "forbidden_paths": forbidden,
            "source_changed": bool(changed), "scope_ok": not forbidden}


def command_for(command_id, repo):
    python = sys.executable
    commands = {
        "LOCAL_ECHO": [python, "-c", "import sys; print('tier-c echo'); print('tier-c stderr', file=sys.stderr)"],
        "LOCAL_FAIL": [python, "-c", "import sys; print('expected failure', file=sys.stderr); sys.exit(7)"],
        "LOCAL_TIMEOUT": [python, "-c", "import time; time.sleep(60)"],
        "LOCAL_MUTATE_ALLOWED": [python, "-c", "from pathlib import Path; Path('allowed/worker.txt').write_text('worker\\n')"],
        "LOCAL_MUTATE_FORBIDDEN": [python, "-c", "from pathlib import Path; Path('forbidden.txt').write_text('worker\\n')"],
        "FRYZZING_VERIFY_CLANG": [
            "ssh", "-i", "/Users/pavel/.ssh/windows_builder", "-o", "BatchMode=yes",
            "-o", "ConnectTimeout=8", "pauls@192.168.1.24",
            'cmd /c "C:\\Users\\pauls\\AD04-asahi-windows-compiler\\llvm20\\bin\\clang-cl.exe --version"',
        ],
    }
    return commands.get(command_id)


def write_payload(output, payload):
    output.mkdir(parents=True, exist_ok=False)
    (output / "summary.json").write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    print(json.dumps(payload, sort_keys=True))


def reject(output, reason, code=2):
    payload = {"result": "REJECTED", "reason": reason, "hardware_used": False,
               "automatic_merge": False}
    if output.exists():
        print(json.dumps(payload, sort_keys=True))
    else:
        write_payload(output, payload)
    raise SystemExit(code)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--task", required=True, type=Path)
    p.add_argument("--command-id", required=True)
    p.add_argument("--output", required=True, type=Path)
    a = p.parse_args()
    if not a.task.is_file():
        reject(a.output, "task_missing")
    try:
        task = json.loads(a.task.read_text())
    except json.JSONDecodeError:
        reject(a.output, "task_not_json")
    required = {"task_id", "tier", "input_commit", "repo", "allowed_paths", "source_change_allowed", "hardware_allowed", "commands", "timeout_seconds"}
    if not required.issubset(task) or task["tier"] != "C":
        reject(a.output, "task_schema_invalid")
    repo = Path(task["repo"])
    if not repo.is_dir() or git(repo, "rev-parse", "--is-inside-work-tree").returncode:
        reject(a.output, "repo_invalid")
    if git(repo, "rev-parse", "--verify", f"{task['input_commit']}^{{commit}}").returncode:
        reject(a.output, "input_commit_invalid")
    if a.command_id not in task["commands"]:
        reject(a.output, "command_not_permitted")
    command = command_for(a.command_id, repo)
    if command is None:
        reject(a.output, "command_unknown")
    if task["hardware_allowed"]:
        reject(a.output, "hardware_not_supported_by_tier_c_runner")
    if a.output.exists():
        reject(a.output, "output_exists")
    a.output.mkdir(parents=True)
    stdout_path, stderr_path = a.output / "stdout.log", a.output / "stderr.log"
    timed_out = False
    with stdout_path.open("w") as stdout, stderr_path.open("w") as stderr:
        try:
            proc = subprocess.run(command, cwd=repo, stdout=stdout, stderr=stderr,
                                  text=True, timeout=int(task["timeout_seconds"]), env={**os.environ, "TIER_C_TASK_ID": task["task_id"]})
            exit_code = proc.returncode
        except subprocess.TimeoutExpired:
            timed_out, exit_code = True, None
    scope_result = scope(repo, task["input_commit"], task["allowed_paths"])
    (a.output / "scope.json").write_text(json.dumps(scope_result, indent=2, sort_keys=True) + "\n")
    if timed_out:
        result, reason = "TIMEOUT", "command_timeout"
    elif scope_result["forbidden_paths"] or (scope_result["source_changed"] and not task["source_change_allowed"]):
        result, reason = "CONTRACT_VIOLATION", "scope_or_source_change"
    elif exit_code:
        result, reason = "FAIL", "command_exit_nonzero"
    else:
        result, reason = "PASS", "none"
    payload = {"task_id": task["task_id"], "input_commit": task["input_commit"],
               "command_id": a.command_id, "command": command, "result": result,
               "reason": reason, "exit_code": exit_code, "timed_out": timed_out,
               "scope": scope_result, "raw_evidence": [str(stdout_path), str(stderr_path), str(a.output / "scope.json")],
               "hardware_used": False, "automatic_merge": False}
    (a.output / "summary.json").write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    print(json.dumps(payload, sort_keys=True))
    raise SystemExit(0 if result == "PASS" else 1)


if __name__ == "__main__":
    main()
