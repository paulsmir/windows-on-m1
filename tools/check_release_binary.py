#!/usr/bin/env python3
"""Reject production m1n1 images that retain periodic diagnostic strings."""

import argparse
from pathlib import Path


FORBIDDEN = (
    b"HV FIQ:",
    b"HV TIMER:",
    b"HV TIMER INJECT:",
    b"HV TIMER IAR:",
    b"HV TIMER EOI:",
    b"HV SGI DIAG:",
    b"HV WATCHDOG PERIODIC:",
)


def check(path: Path) -> None:
    data = path.read_bytes()
    present = [token.decode("ascii") for token in FORBIDDEN if token in data]
    if present:
        raise ValueError("release binary contains runtime diagnostics: " + ", ".join(present))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    try:
        check(args.binary)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f"validated quiet release binary: {args.binary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
