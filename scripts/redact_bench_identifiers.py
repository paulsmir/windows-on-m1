#!/usr/bin/env python3
"""Redact machine identifiers from tracked text: the bench host's serial and the absolute home path.

Values come from the untracked bench config (scripts/bench_env.py): AGX_HOST_SERIAL and the home
directory of the user running the tool.  Rules, in order (idempotent):

  1. the serial                                  -> <SERIAL>
  2. the absolute home /Users/<u> (not C:/Users/<u>) -> $HOME
Windows paths of the guest account (C:/Users/<u>, C:\\Users\\<u>) are working paths in guest-side
scripts and are left alone.

  redact_bench_identifiers.py --check            list tracked files that still contain an identifier
  redact_bench_identifiers.py --apply            rewrite them in place (text files only)
  redact_bench_identifiers.py --filter < in > out  redact a stream (for generators that write tracked text)
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bench_env  # noqa: E402


def rules():
    """(compiled pattern, replacement) pairs.  The Mac home path is redacted only where it is not part
    of a Windows path (C:/Users/<name> is the guest account's working folder in guest-side scripts)."""
    serial = bench_env.load(("AGX_HOST_SERIAL",))["AGX_HOST_SERIAL"]
    pairs = [(re.compile(re.escape(serial)), "<SERIAL>")]
    home = str(Path.home())
    if home.startswith("/Users/"):
        pairs.append((re.compile(r"(?<![A-Za-z]:)" + re.escape(home) + r"(?![A-Za-z0-9_-])"), "$HOME"))
    return pairs


def redact(text, pairs):
    for pattern, new in pairs:
        text = pattern.sub(new, text)
    return text


def tracked(root):
    out = subprocess.run(["git", "-C", str(root), "ls-files", "-z"], capture_output=True, check=True).stdout
    return [root / p for p in out.decode().split("\0") if p]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--apply", action="store_true")
    mode.add_argument("--filter", action="store_true")
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = ap.parse_args()
    pairs = rules()
    if args.filter:
        sys.stdout.write(redact(sys.stdin.read(), pairs))
        return 0
    hits = 0
    for path in tracked(args.root):
        if not path.is_file() or path.is_symlink():
            continue
        data = path.read_bytes()
        if b"\0" in data[:8192]:
            continue  # binary
        text = data.decode("utf-8", errors="surrogateescape")
        new = redact(text, pairs)
        if new != text:
            hits += 1
            if args.apply:
                path.write_bytes(new.encode("utf-8", errors="surrogateescape"))
            print(path.relative_to(args.root))
    print("%s %d files" % ("REDACTED" if args.apply else "CONTAINS_IDENTIFIERS", hits), file=sys.stderr)
    return 1 if (args.check and hits) else 0


if __name__ == "__main__":
    sys.exit(main())
