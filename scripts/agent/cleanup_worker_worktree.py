#!/usr/bin/env python3
"""Remove only a clean, explicitly named worker worktree; never prune broadly."""
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
    p.add_argument("--path", required=True, type=Path)
    p.add_argument("--branch", required=True)
    a = p.parse_args()
    if not a.path.is_dir():
        raise SystemExit("refuse: worktree path does not exist")
    current = git(a.path, "branch", "--show-current").stdout.strip()
    dirty = git(a.path, "status", "--porcelain").stdout
    if current != a.branch or dirty:
        raise SystemExit("refuse: branch mismatch or dirty worker worktree")
    result = git(a.repo, "worktree", "remove", str(a.path))
    if result.returncode:
        raise SystemExit(result.stderr)
    print(json.dumps({"removed": str(a.path), "branch_preserved": a.branch}, sort_keys=True))


if __name__ == "__main__":
    main()
