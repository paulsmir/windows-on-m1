#!/usr/bin/env python3
"""Refuse an experimental guest launch without a fresh staged disk receipt."""

import argparse
import hashlib
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path


MINIMUM_FREE_BYTES = 4 * 1024**3
MAXIMUM_RECEIPT_AGE_SECONDS = 30 * 60
MAXIMUM_WER_BYTES = 128 * 1024**2


def timestamp(value):
    parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if parsed.tzinfo is None:
        raise ValueError("timestamp has no time zone")
    return parsed.astimezone(timezone.utc)


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def exact_integer(value, expected):
    return type(value) is int and value == expected


def verify(readiness_path, receipt_path, stage_path, now):
    readiness = json.loads(readiness_path.read_text())
    receipt_bytes = receipt_path.read_bytes()
    receipt = json.loads(receipt_bytes)
    stage = json.loads(stage_path.read_text())
    require(readiness.get("status") == "STOP_AT_GO", "readiness status")
    experiment = readiness.get("experiment")
    require(isinstance(experiment, str) and re.fullmatch(r"EXP[0-9]+", experiment),
            "experiment identity")
    require(receipt.get("Experiment") == experiment and
            stage.get("Experiment") == experiment, "receipt experiment")
    require(receipt.get("Profile") == "experimental-full-owner", "guest profile")
    require(stage.get("PreflightSha256") == hashlib.sha256(receipt_bytes).hexdigest(),
            "staged preflight hash")
    require(receipt.get("Boot") and receipt.get("Boot") == stage.get("Boot"),
            "guest boot identity")
    receipt_time = timestamp(receipt["Utc"])
    stage_time = timestamp(stage["Utc"])
    require(receipt_time <= stage_time <= now, "receipt order")
    require((now - receipt_time).total_seconds() <= MAXIMUM_RECEIPT_AGE_SECONDS,
            "stale guest disk receipt")
    require(exact_integer(receipt.get("Problem"), 28) and
            exact_integer(receipt.get("Present"), 1) and
            exact_integer(receipt.get("Staged"), 1) and
            exact_integer(receipt.get("Arm"), 1), "staged devnode state")
    require(receipt.get("AutoAdminLogon") == "1", "autologon state")
    free_bytes = receipt.get("FreeC")
    require(type(free_bytes) is int and free_bytes >= MINIMUM_FREE_BYTES,
            "C: below 4 GiB")
    package_version = readiness.get("package_version")
    inf_hash = readiness.get("package_sha256", {}).get("AppleAgxRenderAdmission.inf")
    require(package_version and receipt.get("PackageVersion") == package_version and
            stage.get("PackageVersion") == package_version, "package version")
    require(inf_hash and receipt.get("InfSha256") == inf_hash and
            stage.get("InfHash") == inf_hash, "INF hash")
    require(receipt.get("HardwareManifestSha256") ==
            readiness.get("hardware_manifest_sha256"), "hardware manifest hash")
    require(re.fullmatch(r"oem[0-9]+\.inf", stage.get("Inf", "")) is not None and
            stage.get("OrderedShutdown") == "PowerOff" and
            stage.get("NextProfile") == "cold-full-owner", "stage transition")
    require(exact_integer(receipt.get("CrashDumpEnabled"), 2) and
            exact_integer(receipt.get("CrashOverwrite"), 1), "kernel dump policy")
    require(exact_integer(receipt.get("WerDumpType"), 1) and
            exact_integer(receipt.get("WerDumpCount"), 1) and
            exact_integer(receipt.get("WerMaxBytes"), MAXIMUM_WER_BYTES),
            "WER mini/count/budget policy")
    wer_bytes = receipt.get("WerFolderBytes")
    require(type(wer_bytes) is int and 0 <= wer_bytes <= MAXIMUM_WER_BYTES,
            "WER folder above byte budget")
    require(exact_integer(receipt.get("EtlMode"), 2) and
            exact_integer(receipt.get("EtlMaxFileSizeMB"), 256) and
            exact_integer(receipt.get("EtlFileMax"), 0), "ETL cap policy")
    return {"Experiment": experiment, "FreeC": free_bytes,
            "ReceiptSHA256": hashlib.sha256(receipt_bytes).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("readiness", type=Path)
    parser.add_argument("receipt", type=Path)
    parser.add_argument("stage", type=Path)
    parser.add_argument("--now")
    args = parser.parse_args()
    try:
        now = timestamp(args.now) if args.now else datetime.now(timezone.utc)
        result = verify(args.readiness, args.receipt, args.stage, now)
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(f"GUEST_DISK_LAUNCH_GATE_REFUSED: {error}", file=sys.stderr)
        return 1
    print(json.dumps({"Gate": "GUEST_DISK_LAUNCH_GATE_PASS", **result}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
