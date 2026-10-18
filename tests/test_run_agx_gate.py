import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools.agx_contract import load_contract
from tools.artifact_manifest import ARTIFACT_ROLES, J313_GUEST_CONTRACT
from tests.agx_gate_test_support import install_contract_git, write_artifact_fixture


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "run-agx-gate.sh"
CONTRACT_PATH = ROOT / "config" / "j313-agx.json"
ARTIFACT_NAMES = tuple(ARTIFACT_ROLES)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class RunAgxGateTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.artifacts = self.base / "stable"
        self.artifacts.mkdir()
        self.evidence = self.base / "evidence"
        self.test_bin = self.base / "bin"
        self.test_bin.mkdir()
        fake_ps = self.test_bin / "ps"
        fake_ps.write_text("#!/bin/sh\nexit 0\n")
        fake_ps.chmod(0o755)
        contract = load_contract(CONTRACT_PATH)
        install_contract_git(self.test_bin, contract)
        records = {}
        checksum_lines = []
        for index, name in enumerate(ARTIFACT_NAMES, 1):
            path = self.artifacts / name
            write_artifact_fixture(path, name, index, contract)
            digest = sha256(path)
            records[name] = {
                "size": path.stat().st_size,
                "sha256": digest,
                "role": ARTIFACT_ROLES[name],
            }
            checksum_lines.append(f"{digest}  {name}\n")
        manifest = {
            "format_version": 2,
            "platform": "j313",
            "profile": "debug",
            "display": "both",
            "debug": "monitor",
            "compiler": "test compiler",
            "guest_layout_sha256": "a" * 64,
            "guest_contract": J313_GUEST_CONTRACT,
            "root_commit": contract.source.root_commit,
            "source_dirty": False,
            "m1n1_windows_commit": contract.source.m1n1_commit,
            "m1n1_windows_dirty": False,
            "mu_commit": contract.source.mu_commit,
            "mu_dirty": False,
            "artifacts": records,
        }
        (self.artifacts / "MANIFEST.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n"
        )
        (self.artifacts / "SHA256SUMS").write_text("".join(checksum_lines))

    def tearDown(self):
        self.tmp.cleanup()

    def command(self, *extra):
        return [
            "sh",
            str(SCRIPT),
            "--proxy",
            "/dev/cu.test-proxy",
            "--contract",
            str(CONTRACT_PATH),
            "--artifact-dir",
            str(self.artifacts),
            "--evidence-dir",
            str(self.evidence),
            "--cycles",
            "10",
            *extra,
        ]

    def run_script(self, *extra):
        env = os.environ.copy()
        env["PATH"] = f"{self.test_bin}{os.pathsep}{env['PATH']}"
        return subprocess.run(
            self.command(*extra),
            cwd=self.base,
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_dry_run_prints_every_explicit_gate(self):
        result = self.run_script("--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("mode: assisted AGX firmware gate", result.stdout)
        self.assertIn("cycles: 10", result.stdout)
        self.assertIn("cold reset boundary after every cycle", result.stdout)
        self.assertIn(str(CONTRACT_PATH), result.stdout)
        self.assertIn(str(self.artifacts), result.stdout)
        self.assertIn(str(self.evidence), result.stdout)
        self.assertNotIn("run-assisted.sh", result.stdout)

    def test_cycles_other_than_ten_are_rejected(self):
        command = self.command("--dry-run")
        command[command.index("10")] = "9"
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--cycles must be exactly 10", result.stderr)

    def test_missing_manifest_is_rejected(self):
        (self.artifacts / "MANIFEST.json").unlink()
        result = self.run_script("--dry-run")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Artifact manifest not found", result.stderr)

    def test_mismatched_artifact_hash_is_rejected(self):
        (self.artifacts / "m1n1.macho").write_bytes(b"modified")
        result = self.run_script("--dry-run")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("artifact hash mismatch", result.stderr)

    def test_dirty_recovery_directory_is_rejected(self):
        (self.artifacts / "unexpected.bin").write_bytes(b"dirty")
        result = self.run_script("--dry-run")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unexpected recovery entry", result.stderr)

    def test_active_guest_pid_is_rejected(self):
        pid_path = ROOT / "guest.pid"
        previous = pid_path.read_bytes() if pid_path.exists() else None
        try:
            pid_path.write_text(f"{os.getpid()}\n")
            result = self.run_script("--dry-run")
        finally:
            if previous is None:
                pid_path.unlink(missing_ok=True)
            else:
                pid_path.write_bytes(previous)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("guest runner", result.stderr)

    def test_false_gate_result_is_rejected(self):
        result_path = self.base / "gate-result.json"
        result_path.write_text(
            json.dumps(
                {
                    "gate_version": 1,
                    "requested_cycles": 10,
                    "completed_cycles": 10,
                    "cycles": [{"cycle": i, "status": "passed"} for i in range(1, 11)],
                    "verdict": "failed",
                    "windows_launch_permitted": False,
                }
            )
        )
        result = subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" / "agx_gate.py"),
                "verify-result",
                str(result_path),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not permit Windows launch", result.stderr)

    def test_rejections_do_not_modify_normal_launch_files(self):
        protected = [
            ROOT / "scripts" / "run-assisted.sh",
            ROOT / "scripts" / "build-standalone.sh",
            ROOT / "launch_profile.py",
            ROOT / "standalone_image.py",
        ]
        before = {path: sha256(path) for path in protected}
        result = self.run_script("--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        after = {path: sha256(path) for path in protected}
        self.assertEqual(after, before)


if __name__ == "__main__":
    unittest.main()
