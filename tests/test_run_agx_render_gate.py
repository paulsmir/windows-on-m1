import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tests.test_agx_frame_fixture import IDENTITY, EXPECTED_OUTPUT, _base_members
from tools.agx_capture_clear import CaptureInput, package_capture
from tools.agx_contract import load_contract
from tools.artifact_manifest import ARTIFACT_ROLES, J313_GUEST_CONTRACT


ROOT = Path(__file__).resolve().parents[1]
CAPTURE_SCRIPT = ROOT / "scripts" / "capture-agx-clear-frame.sh"
REPLAY_SCRIPT = ROOT / "scripts" / "run-agx-render-gate.sh"
CONTRACT_PATH = ROOT / "config" / "j313-agx.json"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class OperatorFixture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.artifacts = self.base / "stable"
        self.artifacts.mkdir()
        self.evidence = self.base / "evidence"
        self.destination = self.base / "capture"
        self.test_bin = self.base / "bin"
        self.test_bin.mkdir()
        fake_ps = self.test_bin / "ps"
        fake_ps.write_text("#!/bin/sh\nexit 0\n")
        fake_ps.chmod(0o755)

        contract = load_contract(CONTRACT_PATH)
        records = {}
        checksums = []
        for index, (name, role) in enumerate(ARTIFACT_ROLES.items(), 1):
            path = self.artifacts / name
            path.write_bytes(f"fixture-{index}-{name}\n".encode())
            digest = sha256(path)
            records[name] = {"size": path.stat().st_size, "sha256": digest, "role": role}
            checksums.append(f"{digest}  {name}\n")
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
        (self.artifacts / "SHA256SUMS").write_text("".join(checksums))

        self.mesa = self.base / "mesa"
        self.mesa.mkdir()
        subprocess.run(["git", "init", "-q", str(self.mesa)], check=True)
        subprocess.run(["git", "-C", str(self.mesa), "config", "user.name", "test"], check=True)
        subprocess.run(["git", "-C", str(self.mesa), "config", "user.email", "test@example.invalid"], check=True)
        (self.mesa / "PINNED").write_text("pinned Mesa source\n")
        (self.mesa / ".gitignore").write_text("/build/\n")
        self.shim_library = self.mesa / "build" / "src" / "asahi" / "drm-shim" / "libasahi_m1n1_drm_shim.so"
        self.shim_library.parent.mkdir(parents=True)
        self.shim_library.write_bytes(b"\x7fELF" + b"test shim library\n")
        subprocess.run(
            ["git", "-C", str(self.mesa), "add", "PINNED", ".gitignore"],
            check=True,
        )
        subprocess.run(["git", "-C", str(self.mesa), "commit", "-q", "-m", "fixture"], check=True)
        self.mesa_commit = subprocess.run(
            ["git", "-C", str(self.mesa), "rev-parse", "HEAD"],
            check=True, capture_output=True, text=True,
        ).stdout.strip()
        self.identity = self.base / "identity.json"
        identity = dict(IDENTITY)
        identity["mesa_commit"] = self.mesa_commit
        self.identity.write_text(json.dumps(identity, indent=2, sort_keys=True) + "\n")

        self.capture_program = self.base / "agx-clear"
        self.capture_program.write_text("#!/bin/sh\nexit 0\n")
        self.capture_program.chmod(0o755)

        self.fixture = self.base / "fixture"
        self._make_fixture(identity)
        self.protected = [
            ROOT / "scripts" / "run-assisted.sh",
            ROOT / "scripts" / "build-standalone.sh",
            ROOT / "launch_profile.py",
            ROOT / "standalone_image.py",
        ]

    def tearDown(self):
        self.tmp.cleanup()

    def _make_fixture(self, identity):
        import zipfile

        first = self.base / "first.agx"
        second = self.base / "second.agx"
        members = _base_members()
        for path, reverse in ((first, False), (second, True)):
            with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                for name in sorted(members, reverse=reverse):
                    archive.writestr(name, members[name])
        out1 = self.base / "first.rgba"
        out2 = self.base / "second.rgba"
        out1.write_bytes(EXPECTED_OUTPUT)
        out2.write_bytes(EXPECTED_OUTPUT)
        program_hash = sha256(self.capture_program)
        a = CaptureInput(first, out1, identity, program_hash, "cold-a", 0x800000000)
        b = CaptureInput(second, out2, identity, program_hash, "cold-b", 0x810000000)
        package_capture(a, b, capture_program=self.capture_program, destination=self.fixture)

    def env(self):
        env = os.environ.copy()
        env["PATH"] = f"{self.test_bin}{os.pathsep}{env['PATH']}"
        return env

    def assert_protected_unchanged(self, action):
        before = {path: sha256(path) for path in self.protected}
        result = action()
        self.assertEqual({path: sha256(path) for path in self.protected}, before)
        return result

    def capture_command(self, *extra):
        return [
            "sh", str(CAPTURE_SCRIPT),
            "--proxy", "/dev/cu.test-proxy",
            "--contract", str(CONTRACT_PATH),
            "--artifact-dir", str(self.artifacts),
            "--mesa-source", str(self.mesa),
            "--shim-library", str(self.shim_library),
            "--shim-library-sha256", sha256(self.shim_library),
            "--capture-program", str(self.capture_program),
            "--capture-program-sha256", sha256(self.capture_program),
            "--identity", str(self.identity),
            "--destination", str(self.destination),
            *extra,
        ]

    def replay_command(self, *extra):
        return [
            "sh", str(REPLAY_SCRIPT),
            "--proxy", "/dev/cu.test-proxy",
            "--contract", str(CONTRACT_PATH),
            "--artifact-dir", str(self.artifacts),
            "--frame", str(self.fixture / "frame.agx"),
            "--manifest", str(self.fixture / "manifest.json"),
            "--identity", str(self.identity),
            "--evidence-dir", str(self.evidence),
            "--cycles", "10",
            *extra,
        ]

    def run_script(self, command):
        return subprocess.run(
            command, cwd=self.base, env=self.env(), capture_output=True,
            text=True, check=False,
        )


class CaptureOperatorTests(OperatorFixture):
    def test_capture_dry_run_prints_fixed_two_cold_clear_contract(self):
        result = self.assert_protected_unchanged(
            lambda: self.run_script(self.capture_command("--dry-run"))
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        for value in (
            "mode: assisted AGX fixed-clear capture",
            "clear: 16x16 RGBA8 11 22 33 ff",
            "frame dump: enabled",
            "attachment pull: enabled",
            "cold captures: 2",
            "identity policy: unique proxy identity and m1n1 base",
            "reset policy: physical reboot after every capture",
        ):
            self.assertIn(value, result.stdout)

    def test_dirty_mesa_source_is_rejected(self):
        (self.mesa / "DIRTY").write_text("dirty\n")
        result = self.run_script(self.capture_command("--dry-run"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Mesa source is dirty", result.stderr)

    def test_wrong_mesa_commit_is_rejected(self):
        identity = json.loads(self.identity.read_text())
        identity["mesa_commit"] = "f" * 40
        self.identity.write_text(json.dumps(identity))
        result = self.run_script(self.capture_command("--dry-run"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Mesa source commit", result.stderr)

    def test_capture_identity_must_match_contract(self):
        identity = json.loads(self.identity.read_text())
        identity["board"] = "J314"
        self.identity.write_text(json.dumps(identity))
        result = self.run_script(self.capture_command("--dry-run"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("capture identity does not match AGX contract", result.stderr)

    def test_capture_program_must_match_preregistered_hash(self):
        command = self.capture_command("--dry-run")
        command[command.index("--capture-program-sha256") + 1] = "0" * 64
        result = self.run_script(command)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("capture program SHA-256", result.stderr)

    def test_built_shim_library_must_match_preregistered_hash(self):
        command = self.capture_command("--dry-run")
        command[command.index("--shim-library-sha256") + 1] = "0" * 64
        result = self.run_script(command)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("shim library SHA-256", result.stderr)

    def test_shim_library_must_be_an_elf_shared_object(self):
        self.shim_library.write_bytes(b"not an ELF shared object\n")
        command = self.capture_command("--dry-run")
        command[command.index("--shim-library-sha256") + 1] = sha256(self.shim_library)
        result = self.run_script(command)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("shim library is not ELF", result.stderr)

    def test_nonempty_destination_is_rejected(self):
        self.destination.mkdir()
        (self.destination / "stale").write_text("stale")
        result = self.run_script(self.capture_command("--dry-run"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Capture destination is not empty", result.stderr)

    def test_unknown_timeout_option_is_rejected(self):
        result = self.run_script(self.capture_command("--timeout", "1", "--dry-run"))
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("--timeout", result.stdout)

    def test_capture_has_an_emergency_reboot_guard(self):
        source = CAPTURE_SCRIPT.read_text()
        guard = source.index("emergency_reboot")
        armed = source.index("NEEDS_REBOOT=1")
        capture = source.index("ASAHI_SHIM_DUMP=1", armed)
        self.assertLess(guard, armed)
        self.assertLess(armed, capture)

    def test_capture_uses_historical_mesa_ld_preload_contract(self):
        source = CAPTURE_SCRIPT.read_text()
        self.assertIn('LD_PRELOAD="$SHIM_LIBRARY"', source)
        self.assertNotIn('"$SHIM_LAUNCHER" "$CAPTURE_PROGRAM"', source)

    def test_capture_allows_a_pinned_linux_python_runtime(self):
        source = CAPTURE_SCRIPT.read_text()
        self.assertIn('PYTHON=${AGX_CAPTURE_PYTHON:-"$ROOT/proxyenv/bin/python"}', source)

    def test_capture_persists_producer_output_without_masking_failure(self):
        source = CAPTURE_SCRIPT.read_text()
        self.assertIn('PRODUCER_LOG="$CYCLE_DIR/producer.log"', source)
        log = source.index('PRODUCER_LOG="$CYCLE_DIR/producer.log"')
        producer = source.index('timeout --foreground', log)
        redirect = source.index('>"$PRODUCER_LOG" 2>&1', producer)
        replay = source.index('cat "$PRODUCER_LOG"', redirect)
        failed = source.index('exit "$PRODUCER_STATUS"', replay)
        frame_check = source.index('[ -f "$FRAME" ]', failed)

        self.assertLess(log, producer)
        self.assertLess(producer, redirect)
        self.assertLess(redirect, replay)
        self.assertLess(replay, failed)
        self.assertLess(failed, frame_check)


class ReplayOperatorTests(OperatorFixture):
    def test_replay_dry_run_prints_literal_render_contract(self):
        result = self.assert_protected_unchanged(
            lambda: self.run_script(self.replay_command("--dry-run"))
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        for value in (
            "mode: assisted AGX G1R private render gate",
            "context: 63",
            "queue: renderer index 1",
            "work: TA + 3D",
            "completion deadline: 0.5 seconds",
            "cycles: 10",
            "reset policy: physical cold reset after every cycle",
        ):
            self.assertIn(value, result.stdout)

    def test_cycles_other_than_ten_are_rejected(self):
        command = self.replay_command("--dry-run")
        command[command.index("10")] = "9"
        result = self.run_script(command)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--cycles must be exactly 10", result.stderr)

    def test_nonempty_evidence_is_rejected(self):
        self.evidence.mkdir()
        (self.evidence / "stale").write_text("stale")
        result = self.run_script(self.replay_command("--dry-run"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Evidence directory is not empty", result.stderr)

    def test_active_guest_is_rejected(self):
        pid_path = ROOT / "guest.pid"
        previous = pid_path.read_bytes() if pid_path.exists() else None
        try:
            pid_path.write_text(f"{os.getpid()}\n")
            result = self.run_script(self.replay_command("--dry-run"))
        finally:
            if previous is None:
                pid_path.unlink(missing_ok=True)
            else:
                pid_path.write_bytes(previous)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("guest runner", result.stderr)

    def test_recovery_manifest_mismatch_is_rejected(self):
        (self.artifacts / "m1n1.macho").write_bytes(b"changed")
        result = self.run_script(self.replay_command("--dry-run"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("artifact hash mismatch", result.stderr)

    def test_fixture_identity_must_match_contract(self):
        identity = json.loads(self.identity.read_text())
        identity["board"] = "J314"
        self.identity.write_text(json.dumps(identity))
        manifest_path = self.fixture / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["identity"] = identity
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
        result = self.run_script(self.replay_command("--dry-run"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("fixture identity does not match AGX contract", result.stderr)

    def test_changed_fixture_is_rejected(self):
        with (self.fixture / "frame.agx").open("ab") as stream:
            stream.write(b"changed")
        result = self.run_script(self.replay_command("--dry-run"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("fixture", result.stderr.lower())

    def test_timeout_option_is_not_exposed(self):
        result = self.run_script(self.replay_command("--timeout", "1", "--dry-run"))
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("--timeout", result.stdout)

    def test_failed_cycle_path_reboots_before_exit(self):
        source = REPLAY_SCRIPT.read_text()
        run_one = source.index("tools.agx_render_gate run-one")
        reboot = source.index("proxyclient/tools/reboot.py", run_one)
        failed_exit = source.index('if [ "$CYCLE_OK" -ne 1 ]', reboot)
        self.assertLess(run_one, reboot)
        self.assertLess(reboot, failed_exit)

    def test_validated_candidate_is_activated_before_first_render(self):
        source = REPLAY_SCRIPT.read_text()
        helper = source.index("activate_candidate()")
        chainload = source.index("proxyclient/tools/chainload.py", helper)
        candidate = source.index('"$ARTIFACT_DIR/m1n1.macho"', chainload)
        first_activation = source.index("activate_candidate", candidate)
        run_one = source.index("tools.agx_render_gate run-one", first_activation)

        self.assertLess(helper, chainload)
        self.assertLess(chainload, candidate)
        self.assertLess(candidate, first_activation)
        self.assertLess(first_activation, run_one)

    def test_candidate_is_reactivated_after_reset_before_receipt(self):
        source = REPLAY_SCRIPT.read_text()
        run_one = source.index("tools.agx_render_gate run-one")
        reboot = source.index("proxyclient/tools/reboot.py", run_one)
        activation = source.index("activate_candidate", reboot)
        receipt = source.index("tools.agx_render_gate proxy-receipt", activation)

        self.assertLess(run_one, reboot)
        self.assertLess(reboot, activation)
        self.assertLess(activation, receipt)

    def test_runner_never_substitutes_earlier_gates(self):
        source = REPLAY_SCRIPT.read_text()
        self.assertIn("tools.agx_render_gate run-one", source)
        self.assertNotIn("tools.agx_queue_gate run-one", source)
        self.assertNotIn("tools.agx_gate run-one", source)

    def test_replay_has_an_emergency_reboot_guard(self):
        source = REPLAY_SCRIPT.read_text()
        guard = source.index("emergency_reboot")
        armed = source.index("NEEDS_REBOOT=1")
        run_one = source.index("tools.agx_render_gate run-one", armed)
        self.assertLess(guard, armed)
        self.assertLess(armed, run_one)


if __name__ == "__main__":
    unittest.main()
