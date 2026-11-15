#!/usr/bin/env python3
"""Link the git-ignored inputs that run_against_baseline needs into a fresh worktree.

A new git worktree lacks every untracked, git-ignored path. Some gate tests read such
paths: the required-format contract (drivers/apple-agx/mesa/d3d10umd/required-formats-fl10.json)
names evidence directories under investigation/evidence/ (git-ignored), and several tests
read .local/reference/. Without them a fresh worktree reports NEW gate failures that are
environment, not code (A5-F1 gate, 2026-11-15).

usage: link-untracked-gate-inputs.py SOURCE_WORKTREE [TARGET_WORKTREE]

For every evidence directory the contract names and .local/reference that are missing in
TARGET but present in SOURCE, creates a symlink TARGET/path -> SOURCE/path. Never replaces
an existing path, never touches tracked files or submodules (m1n1_windows, mu stay as they
are). Prints one LINKED/PRESENT/MISSING_IN_SOURCE line per path and GATE_INPUTS_OK when
every required path exists in TARGET afterwards (exit 0), else exit 1.
"""
import json
import os
import sys
from pathlib import Path

CONTRACT = Path("drivers/apple-agx/mesa/d3d10umd/required-formats-fl10.json")
EXTRA = [Path(".local/reference")]


def required_paths(target: Path) -> list:
    paths = set(EXTRA)
    contract = target / CONTRACT
    if contract.is_file():
        value = json.loads(contract.read_text(encoding="utf-8"))
        for entry in value.get("entries", []):
            for evidence in entry.get("evidence", []):
                paths.add(Path(evidence))
        sub = value.get("subresource_evidence")
        if isinstance(sub, str):
            paths.add(Path(sub))
    return sorted(paths)


def main(argv: list) -> int:
    if len(argv) not in (2, 3):
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    source = Path(argv[1]).resolve()
    target = Path(argv[2] if len(argv) == 3 else ".").resolve()
    if source == target:
        print("SOURCE and TARGET are the same worktree", file=sys.stderr)
        return 2
    ok = True
    for rel in required_paths(target):
        dst, src = target / rel, source / rel
        if dst.exists():
            print(f"PRESENT {rel}")
            continue
        if not src.exists():
            print(f"MISSING_IN_SOURCE {rel}")
            ok = False
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        os.symlink(src, dst, target_is_directory=src.is_dir())
        print(f"LINKED {rel}")
    if ok and all((target / rel).exists() for rel in required_paths(target)):
        print("GATE_INPUTS_OK")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
