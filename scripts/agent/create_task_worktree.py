#!/usr/bin/env python3
"""Create one isolated, branch-owned worker worktree; never merge or reuse it."""
import argparse
import json
import subprocess
import sys
from pathlib import Path


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def fail(message):
    print(json.dumps({"created": False, "error": message}, sort_keys=True))
    raise SystemExit(2)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--repo", required=True, type=Path)
    p.add_argument("--task-id", required=True)
    p.add_argument("--branch", required=True)
    p.add_argument("--path", required=True, type=Path)
    p.add_argument("--input-commit", required=True)
    a = p.parse_args()
    if not a.task_id.replace("-", "").replace("_", "").isalnum():
        fail("task-id must be alphanumeric with hyphens or underscores")
    if not a.repo.is_dir() or git(a.repo, "rev-parse", "--is-inside-work-tree").returncode:
        fail("repo is not a git worktree")
    if a.path.exists():
        fail("destination already exists")
    common = Path(git(a.repo, "rev-parse", "--git-common-dir").stdout.strip()).resolve()
    if a.path.resolve().parents and common not in [a.path.resolve(), *a.path.resolve().parents]:
        # Worktrees outside the common repository directory are permitted, but only
        # an explicit absolute destination is accepted; no inferred paths exist.
        pass
    if git(a.repo, "rev-parse", "--verify", f"{a.input_commit}^{{commit}}").returncode:
        fail("input commit does not resolve")
    if git(a.repo, "show-ref", "--verify", "--quiet", f"refs/heads/{a.branch}").returncode == 0:
        fail("worker branch already exists")
    result = git(a.repo, "worktree", "add", "--no-track", "-b", a.branch,
                 str(a.path), a.input_commit)
    if result.returncode:
        fail("git worktree add failed: " + result.stderr.strip())
    print(json.dumps({"created": True, "task_id": a.task_id, "branch": a.branch,
                      "worktree": str(a.path), "input_commit": a.input_commit,
                      "automatic_merge": False}, sort_keys=True))


if __name__ == "__main__":
    main()
