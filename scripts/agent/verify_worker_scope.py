#!/usr/bin/env python3
"""Fail closed when a worker changed a path outside its task contract."""
import argparse
import json
import subprocess
from pathlib import Path


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--repo", required=True, type=Path)
    p.add_argument("--input-commit", required=True)
    p.add_argument("--allowed-path", action="append", required=True)
    a = p.parse_args()
    bad = []
    if not a.repo.is_dir() or git(a.repo, "rev-parse", "--is-inside-work-tree").returncode:
        print(json.dumps({"scope_ok": False, "error": "repo is not a git worktree"}))
        raise SystemExit(2)
    if git(a.repo, "rev-parse", "--verify", f"{a.input_commit}^{{commit}}").returncode:
        print(json.dumps({"scope_ok": False, "error": "input commit does not resolve"}))
        raise SystemExit(2)
    tracked = git(a.repo, "diff", "--name-only", a.input_commit, "--").stdout.splitlines()
    untracked = git(a.repo, "ls-files", "--others", "--exclude-standard").stdout.splitlines()
    changed = sorted(set(tracked + untracked))
    allowed = tuple(a.allowed_path)
    for path in changed:
        if not any(path == rule or (rule.endswith("/") and path.startswith(rule)) for rule in allowed):
            bad.append(path)
    payload = {"input_commit": a.input_commit, "allowed_paths": allowed,
               "changed_paths": changed, "forbidden_paths": bad,
               "source_changed": bool(changed), "scope_ok": not bad}
    print(json.dumps(payload, sort_keys=True))
    raise SystemExit(0 if not bad else 1)


if __name__ == "__main__":
    main()
