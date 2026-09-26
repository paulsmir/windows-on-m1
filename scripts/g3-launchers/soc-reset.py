#!/usr/bin/env python3
"""Require a proxy WDT reset and fresh L41/L43 enumeration before chainload."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--timeout", type=float, default=45)
    parser.add_argument("--port", type=Path, action="append", required=True)
    parser.add_argument("--preflight-command-json")
    parser.add_argument("--emergency-recovery", action="store_true")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command or len(args.port) != 2 or args.timeout <= 0:
        parser.error("two ports, a positive timeout, and a reboot command are required")
    if not args.preflight_command_json and not args.emergency_recovery:
        parser.error("durable preflight required before SoC reset")
    if args.preflight_command_json and args.emergency_recovery:
        parser.error("durable preflight and emergency recovery are exclusive")
    if args.preflight_command_json:
        try:
            preflight = json.loads(args.preflight_command_json)
        except json.JSONDecodeError as error:
            parser.error(f"invalid durable preflight command: {error}")
        if not isinstance(preflight, list) or not preflight or not all(
            isinstance(part, str) and part for part in preflight
        ):
            parser.error("invalid durable preflight command")
        checked = subprocess.run(preflight, timeout=30, check=False,
                                 capture_output=True, text=True)
        if checked.returncode != 0:
            parser.error("durable preflight rejected the SoC reset")
        try:
            receipt = json.loads(checked.stdout.strip().splitlines()[-1])
        except (IndexError, json.JSONDecodeError):
            parser.error("durable preflight did not return a receipt")
        if (receipt.get("Verdict") != "PASS" or not receipt.get("AfterBoot")
                or not receipt.get("ManifestSha256")):
            parser.error("durable preflight receipt is incomplete")
    if not all(port.exists() for port in args.port):
        parser.error("both proxy ports must be present before P_REBOOT")

    deadline = time.monotonic() + args.timeout
    child = subprocess.Popen(command)
    detached = False
    try:
        while time.monotonic() < deadline:
            present = [port.exists() for port in args.port]
            if not detached and not any(present):
                detached = True
                print("SoC reset: L41/L43 detached", flush=True)
            elif detached and all(present):
                remaining = max(0.1, deadline - time.monotonic())
                if child.wait(timeout=remaining) != 0:
                    raise RuntimeError("P_REBOOT failed")
                print("SoC reset: L41/L43 re-enumerated", flush=True)
                return 0
            if child.poll() not in (None, 0):
                raise RuntimeError("P_REBOOT failed")
            time.sleep(0.05)
        raise RuntimeError("timeout waiting for L41/L43 detach and re-enumeration")
    except (RuntimeError, subprocess.TimeoutExpired) as error:
        if child.poll() is None:
            child.terminate()
            child.wait(timeout=5)
        print(f"SoC reset: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
