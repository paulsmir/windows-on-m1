import os
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class PublicScriptTests(unittest.TestCase):
    def test_assisted_scripts_exist_and_do_not_embed_private_devices(self):
        names = (
            "build-development.sh",
            "run-assisted.sh",
            "reset-assisted.sh",
            "display-assisted.sh",
            "log-assisted.sh",
        )
        for name in names:
            path = ROOT / "scripts" / name
            self.assertTrue(path.is_file(), name)
            text = path.read_text(encoding="utf-8")
            self.assertNotIn("/Users/pavel", text)
            self.assertNotIn("C02HDNCCQ6L41", text)
            self.assertNotIn("C02HDNCCQ6L43", text)
            self.assertIn('dirname -- "$0"', text)

    def test_standalone_monitor_wrapper_is_location_independent_and_dry_run_safe(self):
        path = ROOT / "scripts/log-standalone.sh"
        self.assertTrue(path.is_file())
        text = path.read_text(encoding="utf-8")
        self.assertIn('dirname -- "$0"', text)
        self.assertNotIn("/Users/pavel", text)
        self.assertNotIn("C02HDNCCQ6L41", text)

        result = subprocess.run(
            [
                "sh",
                str(path),
                "--console",
                "/dev/cu.test-console",
                "--vuart",
                "/dev/cu.test-vuart",
                "--output",
                "test-captures",
                "--dry-run",
            ],
            cwd="/tmp",
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("tools/standalone_monitor.py", result.stdout)
        self.assertIn("--console /dev/cu.test-console", result.stdout)
        self.assertIn("--vuart /dev/cu.test-vuart", result.stdout)
        self.assertIn("--output test-captures", result.stdout)

    def test_development_build_dry_run_names_replaceable_components(self):
        result = subprocess.run(
            ["sh", str(ROOT / "scripts/build-development.sh"), "--dry-run"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertIn("m1n1.macho", result.stdout)
        self.assertIn("J313_EFI.fd", result.stdout)
        self.assertIn("chainload.py", result.stdout)

    def test_development_build_forwards_standalone_profile(self):
        result = subprocess.run(
            [
                "sh", str(ROOT / "scripts/build-development.sh"),
                "--dry-run", "--display", "virtual", "--debug", "uart",
            ],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--display virtual --debug uart", result.stdout)

    def test_run_assisted_dry_run_describes_order_and_selected_paths(self):
        command = [
            "sh",
            str(ROOT / "scripts/run-assisted.sh"),
            "--dry-run",
            "--proxy",
            "/dev/cu.test-proxy",
            "--vuart",
            "/dev/cu.test-vuart",
            "--firmware",
            "firmware/test.fd",
            "--ramdisk",
            "images/test.img",
        ]
        result = subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
        self.assertIn("reader-before-guest", result.stdout)
        self.assertIn("/dev/cu.test-proxy", result.stdout)
        self.assertIn("/dev/cu.test-vuart", result.stdout)
        self.assertIn("firmware/test.fd", result.stdout)
        self.assertIn("images/test.img", result.stdout)

    def test_assisted_workers_are_detached_from_launcher_shell(self):
        text = (ROOT / "scripts/run-assisted.sh").read_text(encoding="utf-8")

        self.assertGreaterEqual(text.count("nohup "), 2)
        self.assertGreaterEqual(text.count("</dev/null"), 2)

    def test_display_and_log_dry_runs_are_hardware_free(self):
        cases = {
            "display-assisted.sh": "http://127.0.0.1:8766/",
            "log-assisted.sh": "http://127.0.0.1:8765/",
        }
        for name, expected in cases.items():
            result = subprocess.run(
                ["sh", str(ROOT / "scripts" / name), "--dry-run"],
                cwd=ROOT,
                check=True,
                capture_output=True,
                text=True,
                env={**os.environ, "PATH": "/usr/bin:/bin:/usr/sbin:/sbin"},
            )
            self.assertIn(expected, result.stdout)

    def test_run_windows_defaults_to_quiet_standalone_physical(self):
        result = subprocess.run(
            ["sh", str(ROOT / "scripts/run-windows.sh"), "--dry-run"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )

        self.assertIn("execution: standalone", result.stdout)
        self.assertIn("display: physical", result.stdout)
        self.assertIn("debug: off", result.stdout)
        self.assertIn("virtual UART: disabled", result.stdout)
        self.assertIn("USB framebuffer: disabled", result.stdout)
        self.assertIn("telemetry: disabled", result.stdout)

    def test_quiet_assisted_physical_does_not_require_vuart(self):
        result = subprocess.run(
            [
                "sh",
                str(ROOT / "scripts/run-assisted.sh"),
                "--dry-run",
                "--proxy",
                "/dev/cu.test-proxy",
                "--display",
                "physical",
                "--debug",
                "off",
            ],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )

        self.assertIn("display: physical", result.stdout)
        self.assertIn("debug: off", result.stdout)
        self.assertIn("virtual UART: disabled", result.stdout)
        self.assertIn("USB framebuffer: disabled", result.stdout)
        self.assertIn("telemetry: disabled", result.stdout)
        self.assertNotIn("reader-before-guest", result.stdout)

    def test_both_full_resolves_every_observer(self):
        result = subprocess.run(
            [
                "sh",
                str(ROOT / "scripts/run-windows.sh"),
                "--execution",
                "assisted",
                "--display",
                "both",
                "--debug",
                "full",
                "--proxy",
                "/dev/cu.test-proxy",
                "--vuart",
                "/dev/cu.test-vuart",
                "--dry-run",
            ],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )

        self.assertIn("execution: assisted", result.stdout)
        self.assertIn("display: both", result.stdout)
        self.assertIn("debug: full", result.stdout)
        self.assertIn("virtual UART: /dev/cu.test-vuart", result.stdout)
        self.assertIn("USB framebuffer: enabled", result.stdout)
        self.assertIn("telemetry: enabled", result.stdout)

    def test_assisted_dry_run_can_include_matching_m1n1_chainload(self):
        result = subprocess.run(
            [
                "sh", str(ROOT / "scripts/run-windows.sh"),
                "--execution", "assisted",
                "--display", "both",
                "--debug", "full",
                "--proxy", "/dev/cu.test-proxy",
                "--vuart", "/dev/cu.test-vuart",
                "--chainload",
                "--m1n1", "dist/j313/test.macho",
                "--dry-run",
            ],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("chainload: dist/j313/test.macho", result.stdout)

    def test_launchers_reject_unknown_profile_values(self):
        cases = (
            ("--execution", "remote"),
            ("--display", "mirror"),
            ("--debug", "yes"),
        )
        for option, value in cases:
            with self.subTest(option=option, value=value):
                result = subprocess.run(
                    [
                        "sh",
                        str(ROOT / "scripts/run-windows.sh"),
                        option,
                        value,
                        "--dry-run",
                    ],
                    cwd=ROOT,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(result.returncode, 2)


if __name__ == "__main__":
    unittest.main()
