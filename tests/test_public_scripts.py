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


if __name__ == "__main__":
    unittest.main()
