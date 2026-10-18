#!/usr/bin/env python3
"""After an armed ordered power-off, enter the exact cold full-owner image."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_transition(root, receipt, artifacts, inf_sha256):
    if receipt.get("Arm") != 1:
        raise ValueError("G3 arm receipt missing")
    if receipt.get("OrderedShutdown") != "PowerOff":
        raise ValueError("ordered power-off receipt missing")
    if receipt.get("NextProfile") != "cold-full-owner":
        raise ValueError("armed transition targets another profile")
    if receipt.get("InfSha256", "").lower() != inf_sha256.lower():
        raise ValueError("staged INF identity mismatch")
    for name, expected in artifacts.items():
        path = root / name
        if Path(name).is_absolute() or ".." in Path(name).parts or not path.is_file():
            raise ValueError(f"artifact absent or outside experiment: {name}")
        if sha256(path).lower() != expected.lower():
            raise ValueError(f"artifact hash mismatch: {name}")


def ssh_alive(user, host, key, known_hosts):
    command = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5",
               "-o", "IdentitiesOnly=yes", "-i", str(key),
               "-o", "HostKeyAlgorithms=ssh-ed25519",
               "-o", f"UserKnownHostsFile={known_hosts}",
               "-o", "StrictHostKeyChecking=yes", f"{user}@{host}", "hostname"]
    return subprocess.run(command, stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL, timeout=12).returncode == 0


def active_launcher():
    result = subprocess.run(["pgrep", "-fl", "run_uefi.py"],
                            capture_output=True, text=True, check=False)
    return result.returncode == 0 and bool(result.stdout.strip())


def wait_for_ordered_poweroff(args):
    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        if (not ssh_alive(args.guest_user, args.guest_host, args.ssh_key, args.known_hosts)
                and not active_launcher()
                and all(path.exists() for path in args.proxy_port)):
            for _ in range(3):
                time.sleep(2)
                if ssh_alive(args.guest_user, args.guest_host, args.ssh_key, args.known_hosts):
                    raise RuntimeError("guest returned before cold full-owner launch")
            return
        time.sleep(2)
    raise RuntimeError("ordered power-off/proxy boundary not observed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--stage-receipt", type=Path, required=True)
    parser.add_argument("--guest-user", required=True)
    parser.add_argument("--guest-host", required=True)
    parser.add_argument("--ssh-key", type=Path, required=True)
    parser.add_argument("--known-hosts", type=Path, required=True)
    parser.add_argument("--proxy-port", type=Path, action="append", required=True)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    receipt = json.loads(args.stage_receipt.read_text())
    root = args.manifest.parent
    if manifest.get("launch_profile") != "cold-full-owner":
        raise ValueError("manifest launch profile mismatch")
    if receipt.get("ExperimentManifestSha256", "").lower() != sha256(args.manifest):
        raise ValueError("experiment manifest identity mismatch")
    artifacts = manifest["artifact_sha256"]
    launcher_name = manifest["launcher"]
    if launcher_name not in artifacts or Path(launcher_name).name != "full-owner-direct.sh":
        raise ValueError("unverified full-owner launcher")
    validate_transition(root, receipt, artifacts, manifest["inf_sha256"])
    wait_for_ordered_poweroff(args)
    launcher = root / launcher_name
    print("ORDERED_POWER_OFF_PASS; entering cold full-owner", flush=True)
    os.execv(str(launcher), [str(launcher)])


if __name__ == "__main__":
    main()
