from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/run-agx-capture-container.sh"
HELPER = ROOT / "tools/agx-capture-container/run-capture.sh"
PROBE_SCRIPT = ROOT / "scripts/probe-agx-capture-transport.sh"
PROBE_HELPER = ROOT / "tools/agx-capture-container/probe-transport.sh"


class RunAgxCaptureContainerTests(unittest.TestCase):
    def command(self, *extra: str) -> list[str]:
        return [
            str(SCRIPT),
            "--proxy", "/dev/cu.test-m1n1",
            "--contract", "config/j313-agx.json",
            "--artifact-dir", ".local/recovery/STABLE-j313-8core-native-input-v1",
            "--identity", ".local/agx-capture/identity.json",
            "--destination", "/tmp/agx-capture-exp",
            "--bridge-port", "43137",
            *extra,
        ]

    def test_dry_run_prints_read_only_repo_and_reconnecting_serial_bridge(self):
        result = subprocess.run(
            self.command("--dry-run"),
            cwd="/tmp",
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        output = result.stdout
        expected_in_order = (
            "TCP-LISTEN:43137,bind=127.0.0.1,reuseaddr,fork",
            "FILE:/dev/cu.test-m1n1,raw,echo=0",
            "docker run --rm",
            "host.docker.internal",
            "<repository-root>:/work:ro",
            "/tmp:/capture-host:rw",
            "tools/agx-capture-container/run-capture.sh:/opt/agx-capture/run-capture-public.sh:ro",
            "/opt/agx-capture/run-capture-public.sh",
            "--destination /capture-host/agx-capture-exp",
        )
        cursor = 0
        for item in expected_in_order:
            found = output.find(item, cursor)
            self.assertGreaterEqual(found, 0, f"missing or out-of-order step: {item}")
            cursor = found + len(item)
        self.assertNotIn("/Users/pavel", output)
        self.assertNotIn(" /opt/agx-capture/run-capture.sh ", output)

    def test_container_helper_reconnects_pty_and_uses_pinned_linux_python(self):
        source = HELPER.read_text(encoding="utf-8")
        self.assertIn("PTY,link=/tmp/m1n1-proxy,raw,echo=0,ignoreeof", source)
        self.assertNotIn("wait-slave", source)
        self.assertIn("TCP:host.docker.internal:${AGX_BRIDGE_PORT}", source)
        self.assertIn("sleep 1", source)
        self.assertIn("AGX_CAPTURE_PYTHON=python3", source)
        self.assertIn("verify-agx-capture-env.py", source)

    def test_capture_exports_both_python_roots_for_every_subcommand(self):
        source = (ROOT / "scripts/capture-agx-clear-frame.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            'export PYTHONPATH="$ROOT:$ROOT/m1n1_windows/proxyclient', source
        )

    def test_transport_probe_is_bounded_read_only_and_reboot_separated(self):
        helper = PROBE_HELPER.read_text(encoding="utf-8")
        required = (
            "probe-proxy-identity.py",
            "before.json",
            "reboot.py",
            "after.json",
            "20",
            "transport-receipt.json",
            "PTY,link=/tmp/m1n1-proxy,raw,echo=0,ignoreeof",
        )
        for item in required:
            self.assertIn(item, helper)
        self.assertNotIn("capture-agx-clear-frame", helper)
        self.assertNotIn("LD_PRELOAD", helper)
        self.assertNotIn("pmgr_adt_clocks_enable", helper)

    def test_transport_probe_dry_run_has_no_hardware_side_effect(self):
        result = subprocess.run(
            [
                str(PROBE_SCRIPT),
                "--proxy", "/dev/cu.test-m1n1",
                "--destination", "/tmp/agx-transport-probe",
                "--bridge-port", "43138",
                "--dry-run",
            ],
            cwd="/tmp",
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("TCP-LISTEN:43138", result.stdout)
        self.assertIn("repository-root>:/work:ro", result.stdout)
        self.assertIn("probe-transport.sh", result.stdout)
        self.assertIn("/tmp:/capture-host:rw", result.stdout)

    def test_unknown_option_is_rejected(self):
        result = subprocess.run(
            [str(SCRIPT), "--surprise"],
            cwd="/tmp",
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("unknown option", result.stderr)


if __name__ == "__main__":
    unittest.main()
