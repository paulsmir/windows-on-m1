import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GATE = ROOT / "scripts" / "verify_guest_disk_launch.py"


class GuestDiskLaunchGateTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.now = datetime(2026, 10, 1, 0, 0, tzinfo=timezone.utc)
        self.readiness = {
            "experiment": "EXP907",
            "status": "STOP_AT_GO",
            "package_version": "30.0.907.0",
            "hardware_manifest_sha256": "a" * 64,
            "package_sha256": {"AppleAgxRenderAdmission.inf": "b" * 64},
            "launch_gate_sha256": hashlib.sha256(GATE.read_bytes()).hexdigest(),
        }
        self.receipt = {
            "Experiment": "EXP907",
            "Profile": "experimental-full-owner",
            "Utc": (self.now - timedelta(minutes=2)).isoformat(),
            "Boot": "2026-09-30T23:00:00Z",
            "Problem": 28,
            "Present": 1,
            "Staged": 1,
            "Arm": 1,
            "FreeC": 5 * 1024**3,
            "AutoAdminLogon": "1",
            "PackageVersion": "30.0.907.0",
            "InfSha256": "b" * 64,
            "HardwareManifestSha256": "a" * 64,
            "CrashDumpEnabled": 2,
            "CrashOverwrite": 1,
            "WerDumpType": 1,
            "WerDumpCount": 1,
            "WerFolderBytes": 0,
            "WerMaxBytes": 128 * 1024**2,
            "EtlMode": 2,
            "EtlMaxFileSizeMB": 256,
            "EtlFileMax": 0,
        }
        self.stage = {
            "Experiment": "EXP907",
            "Utc": (self.now - timedelta(minutes=1)).isoformat(),
            "Boot": self.receipt["Boot"],
            "Inf": "oem5.inf",
            "InfHash": "b" * 64,
            "PackageVersion": "30.0.907.0",
            "OrderedShutdown": "PowerOff",
            "NextProfile": "cold-full-owner",
        }

    def run_gate(self):
        readiness_path = self.root / "readiness.json"
        receipt_path = self.root / "guest-preflight.json"
        stage_path = self.root / "stage-receipt.json"
        readiness_path.write_text(json.dumps(self.readiness))
        receipt_path.write_text(json.dumps(self.receipt))
        self.stage.setdefault("PreflightSha256", hashlib.sha256(receipt_path.read_bytes()).hexdigest())
        stage_path.write_text(json.dumps(self.stage))
        return subprocess.run(
            [sys.executable, str(GATE), str(readiness_path), str(receipt_path),
             str(stage_path), "--now", self.now.isoformat()],
            text=True, capture_output=True, check=False,
        )

    def test_valid_staged_guest_passes(self):
        result = self.run_gate()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_below_four_gib_refuses(self):
        self.receipt["FreeC"] = 4 * 1024**3 - 1
        self.assertNotEqual(self.run_gate().returncode, 0)

    def test_stale_receipt_refuses(self):
        self.receipt["Utc"] = (self.now - timedelta(hours=2)).isoformat()
        self.assertNotEqual(self.run_gate().returncode, 0)

    def test_changed_receipt_hash_refuses(self):
        self.stage["PreflightSha256"] = "c" * 64
        result = self.run_gate()
        self.assertNotEqual(result.returncode, 0)

    def test_changed_launch_gate_hash_refuses(self):
        self.readiness["launch_gate_sha256"] = "d" * 64
        self.assertNotEqual(self.run_gate().returncode, 0)

    def test_unsafe_trace_or_dump_policy_refuses(self):
        self.receipt["EtlMaxFileSizeMB"] = 512
        self.assertNotEqual(self.run_gate().returncode, 0)
        self.receipt["EtlMaxFileSizeMB"] = 256
        self.receipt["WerFolderBytes"] = 128 * 1024**2 + 1
        self.assertNotEqual(self.run_gate().returncode, 0)


if __name__ == "__main__":
    unittest.main()
