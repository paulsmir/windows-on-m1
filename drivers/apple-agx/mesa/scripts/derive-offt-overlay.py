#!/usr/bin/env python3
"""Derive only the proven internal AGX binary-offset Windows overlay."""
import argparse
import hashlib
from pathlib import Path

HEADER_SHA256 = "c41df12679c1692cc50617e81d57eb03cf906d8c5fbada482b89fc6dde0c7741"
PACK_SHA256 = "f594448db4ccfe5d4f98f849c3f1e08b662264702f5de40a4c97d34be180cf00"

HEADER_REPLACEMENTS = (
    ("   bool kill : 1;", "   unsigned kill : 1;"),
    ("   bool cache   : 1;", "   unsigned cache   : 1;"),
    ("   bool discard : 1;", "   unsigned discard : 1;"),
    ("   bool abs : 1;", "   unsigned abs : 1;"),
    ("   bool neg : 1;", "   unsigned neg : 1;"),
    ("   bool memory : 1;", "   unsigned memory : 1;"),
    ("   enum agx_size size       : 2;", "   unsigned size       : 2;"),
    ("   enum agx_index_type type : 3;", "   unsigned type : 3;"),
    ("   bool has_reg     : 1;", "   unsigned has_reg     : 1;"),
    ("   off_t offset, last_offset;", "   unsigned offset, last_offset;"),
)


def transform(text, replacements):
    for old, new in replacements:
        if text.count(old) != 1:
            raise SystemExit(f"expected exactly one source anchor: {old!r}")
        text = text.replace(old, new)
    return text


def checked(path, expected):
    text = path.read_text(encoding="utf-8")
    digest = hashlib.sha256(text.encode()).hexdigest()
    if digest != expected:
        raise SystemExit(f"pinned source hash mismatch for {path}: {digest}")
    return text


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--header-input", required=True, type=Path)
    p.add_argument("--pack-input", required=True, type=Path)
    p.add_argument("--header-output", required=True, type=Path)
    p.add_argument("--pack-output", required=True, type=Path)
    p.add_argument("--diff-output", required=True, type=Path)
    a = p.parse_args()
    header = checked(a.header_input, HEADER_SHA256)
    pack = checked(a.pack_input, PACK_SHA256)
    derived_header = transform(header, HEADER_REPLACEMENTS)
    derived_pack = transform(pack, (
        ("   off_t offset;", "   unsigned offset;"),
        ("   off_t target =", "   unsigned target ="),
    ))
    a.header_output.parent.mkdir(parents=True, exist_ok=True)
    a.pack_output.parent.mkdir(parents=True, exist_ok=True)
    a.diff_output.parent.mkdir(parents=True, exist_ok=True)
    a.header_output.write_text(derived_header, encoding="utf-8")
    a.pack_output.write_text(derived_pack, encoding="utf-8")
    import difflib
    diff = list(difflib.unified_diff(
        header.splitlines(True), derived_header.splitlines(True),
        fromfile=str(a.header_input), tofile=str(a.header_output)))
    diff += list(difflib.unified_diff(
        pack.splitlines(True), derived_pack.splitlines(True),
        fromfile=str(a.pack_input), tofile=str(a.pack_output)))
    a.diff_output.write_text("".join(diff), encoding="utf-8")
    changed = [line for line in diff if line.startswith(("+", "-")) and not line.startswith(("+++", "---"))]
    if len(changed) != 24:
        raise SystemExit(f"unexpected derived diff size: {len(changed)} changed lines")
    print(f"header_sha256={HEADER_SHA256}")
    print(f"pack_sha256={PACK_SHA256}")
    print(f"diff={a.diff_output}")


if __name__ == "__main__":
    main()
