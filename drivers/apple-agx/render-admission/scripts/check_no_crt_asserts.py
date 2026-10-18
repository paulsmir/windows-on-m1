#!/usr/bin/env python3
"""Reject packaged binaries containing UTF-16LE UCRT _wassert file paths."""

import argparse
import hashlib
import json
from pathlib import Path
import re


WIDE_ASCII = re.compile(rb"(?:[\x20-\x7e]\x00){6,}")
SOURCE_PATH = re.compile(r"^[\x20-\x7e]*[\\/][\x20-\x7e]*\.(?:c|cc|cpp|h|hpp)$", re.I)


def inspect(path):
    data = path.read_bytes()
    matches = []
    for run in WIDE_ASCII.finditer(data):
        value = run.group().decode("utf-16le")
        if SOURCE_PATH.search(value):
            matches.append(value)
    return {"path": str(path), "sha256": hashlib.sha256(data).hexdigest(),
            "assert_paths": len(matches), "examples": matches[:3]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--allow", action="store_true",
                        help="report assertion paths without rejecting diagnostic packages")
    args = parser.parse_args()
    found = False
    for path in args.files:
        result = inspect(path)
        print(json.dumps(result))
        found |= result["assert_paths"] > 0
    return int(found and not args.allow)


if __name__ == "__main__":
    raise SystemExit(main())
