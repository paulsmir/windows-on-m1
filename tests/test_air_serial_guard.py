import fcntl
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GUARD = ROOT / "scripts/air-serial-guard.py"


class AirSerialGuardTests(unittest.TestCase):
    def test_busy_port_rejects_before_command(self):
        with tempfile.TemporaryDirectory() as directory:
            port = Path(directory) / "serial"
            marker = Path(directory) / "launched"
            with port.open("w+"):
                result = subprocess.run(
                    [sys.executable, str(GUARD), "--port", str(port), "--",
                     sys.executable, "-c", f"open({str(marker)!r}, 'w').close()"],
                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 73)
            self.assertIn("busy", result.stderr)
            self.assertFalse(marker.exists())

    def test_lock_rejects_before_command(self):
        with tempfile.TemporaryDirectory() as directory:
            port = Path(directory) / "serial"
            port.touch()
            marker = Path(directory) / "launched"
            lock_path = ROOT / ".local/air.lock"
            lock_path.parent.mkdir(parents=True, exist_ok=True)
            with lock_path.open("a+") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                result = subprocess.run(
                    [sys.executable, str(GUARD), "--port", str(port), "--",
                     sys.executable, "-c", f"open({str(marker)!r}, 'w').close()"],
                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 73)
            self.assertIn("launcher", result.stderr)
            self.assertFalse(marker.exists())


if __name__ == "__main__":
    unittest.main()
