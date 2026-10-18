#!/usr/bin/env python3
"""Hold the Air launcher lock and reject serial ports already in use."""

import argparse
import fcntl
import os
from pathlib import Path
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", action="append", required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("a launcher command is required")
    lock_path = Path(__file__).resolve().parents[1] / ".local/air.lock"
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    with lock_path.open("a+") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print("Air serial launcher already owns .local/air.lock", file=sys.stderr)
            return 73
        for port in args.port:
            if not Path(port).exists():
                print(f"Air serial port absent: {port}", file=sys.stderr)
                return 74
            owner = subprocess.run(
                ["lsof", "-nP", "-t", "--", port],
                capture_output=True, text=True, check=False)
            if owner.returncode not in (0, 1):
                print(f"Cannot inspect Air serial port {port}: {owner.stderr.strip()}",
                      file=sys.stderr)
                return 74
            if owner.stdout.strip():
                print(f"Air serial port busy: {port}; PID {owner.stdout.strip()}",
                      file=sys.stderr)
                return 73
        env = os.environ.copy()
        env["AIR_SERIAL_LOCK_ACTIVE"] = "1"
        return subprocess.call(command, env=env)


if __name__ == "__main__":
    raise SystemExit(main())
