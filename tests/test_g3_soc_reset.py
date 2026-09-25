import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RESET = ROOT / "scripts/g3-launchers/soc-reset.py"
LAUNCHERS = ROOT / "scripts/g3-launchers"


class SocResetTest(unittest.TestCase):
    def run_reset(self, action):
        with tempfile.TemporaryDirectory() as directory:
            ports = [Path(directory) / "L41", Path(directory) / "L43"]
            for port in ports:
                port.touch()
            command = [
                sys.executable,
                str(RESET),
                "--timeout", "2",
                "--port", str(ports[0]),
                "--port", str(ports[1]),
                "--",
                sys.executable,
                "-c",
                action,
                str(ports[0]),
                str(ports[1]),
            ]
            return subprocess.run(command, capture_output=True, text=True)

    def test_reset_requires_detach_and_reenumeration(self):
        result = self.run_reset(
            "import pathlib,sys,time; p=[pathlib.Path(x) for x in sys.argv[1:]];"
            "time.sleep(.1); [x.unlink() for x in p]; time.sleep(.1);"
            "[x.touch() for x in p]"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("re-enumerated", result.stdout)

    def test_reset_rejects_missing_detach(self):
        result = self.run_reset("pass")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("detach", result.stderr)

    def test_each_launcher_resets_before_chainload(self):
        for name in ("full-owner.sh", "emergency.sh", "ordinary.sh"):
            with self.subTest(name=name):
                script = (LAUNCHERS / name).read_text()
                self.assertLess(script.index("soc-reset.py"), script.index("chainload.py"))


if __name__ == "__main__":
    unittest.main()
