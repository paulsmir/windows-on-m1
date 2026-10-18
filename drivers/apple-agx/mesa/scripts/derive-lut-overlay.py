#!/usr/bin/env python3
import argparse
import hashlib
import difflib
from pathlib import Path

EXPECTED_SHA256 = "23caf6713955e6d9ad6c4300a83a0421cc92e6bcef23688216172f98c9bef456"


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--input", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--diff", required=True, type=Path)
    a = p.parse_args()
    source = a.input.read_text(encoding="utf-8")
    digest = hashlib.sha256(source.encode()).hexdigest()
    if digest != EXPECTED_SHA256:
        raise SystemExit(f"util/lut.h hash mismatch: {digest}")
    old = "#if !defined(_MSC_VER)"
    new = "#if !defined(_MSC_VER) || defined(__clang__)"
    if source.count(old) != 1:
        raise SystemExit("guard anchor count mismatch")
    derived = source.replace(old, new)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.diff.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(derived, encoding="utf-8")
    diff = "".join(difflib.unified_diff(source.splitlines(True), derived.splitlines(True), fromfile=str(a.input), tofile=str(a.output)))
    a.diff.write_text(diff, encoding="utf-8")
    changed = [line for line in diff.splitlines() if line.startswith(("+", "-")) and not line.startswith(("+++", "---"))]
    if len(changed) != 2:
        raise SystemExit(f"unexpected derived diff: {len(changed)} lines")
    print(f"input_sha256={digest}")
    print(f"output={a.output}")
    print(f"diff={a.diff}")


if __name__ == "__main__":
    main()
