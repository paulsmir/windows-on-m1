#!/usr/bin/env python3
"""Derive only the approved homogeneous-unsigned agx_index Windows header."""
import argparse
import hashlib
from pathlib import Path


EXPECTED_SHA256 = "c41df12679c1692cc50617e81d57eb03cf906d8c5fbada482b89fc6dde0c7741"
REPLACEMENTS = (
    ("   bool kill : 1;", "   unsigned kill : 1;"),
    ("   bool cache   : 1;", "   unsigned cache   : 1;"),
    ("   bool discard : 1;", "   unsigned discard : 1;"),
    ("   bool abs : 1;", "   unsigned abs : 1;"),
    ("   bool neg : 1;", "   unsigned neg : 1;"),
    ("   bool memory : 1;", "   unsigned memory : 1;"),
    ("   enum agx_size size       : 2;", "   unsigned size       : 2;"),
    ("   enum agx_index_type type : 3;", "   unsigned type : 3;"),
    ("   bool has_reg     : 1;", "   unsigned has_reg     : 1;"),
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--diff", required=True, type=Path)
    args = parser.parse_args()
    source = args.input.read_text(encoding="utf-8")
    digest = hashlib.sha256(source.encode()).hexdigest()
    if digest != EXPECTED_SHA256:
        raise SystemExit(f"pinned agx_compiler.h hash mismatch: {digest}")
    derived = source
    for old, new in REPLACEMENTS:
        if derived.count(old) != 1:
            raise SystemExit(f"expected exactly one source anchor: {old!r}")
        derived = derived.replace(old, new)
    if derived == source:
        raise SystemExit("overlay made no transformation")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.diff.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(derived, encoding="utf-8")
    import difflib
    diff = difflib.unified_diff(source.splitlines(True), derived.splitlines(True),
                                fromfile=str(args.input), tofile=str(args.output))
    args.diff.write_text("".join(diff), encoding="utf-8")
    if sum(1 for line in args.diff.read_text().splitlines() if line.startswith(('+', '-')) and not line.startswith(('+++', '---'))) != 18:
        raise SystemExit("derived diff changed more than the nine approved type lines")
    print(f"input_sha256={digest}")
    print(f"output={args.output}")
    print(f"diff={args.diff}")


if __name__ == "__main__":
    main()
