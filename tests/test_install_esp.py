from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/install-esp.sh"


class InstallEspTests(unittest.TestCase):
    def run_dry(self, *arguments):
        return subprocess.run(
            [str(SCRIPT), "--dry-run", *arguments],
            cwd="/tmp",
            text=True,
            capture_output=True,
            check=False,
        )

    def test_install_is_scoped_backed_up_atomic_and_verified(self):
        result = self.run_dry(
            "install", "--disk", "disk9s4", "--image", "dist/j313/boot.bin"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        output = result.stdout
        for item in (
            "validate standalone manifest",
            "diskutil mount disk9s4",
            "/m1n1/boot.bin",
            "create backup once",
            "copy to temporary sibling",
            "verify SHA-256",
            "atomic rename",
        ):
            self.assertIn(item, output)
        self.assertNotIn("disk0s4", output)
        self.assertNotIn("/Users/pavel", output)

    def test_restore_uses_the_same_scoped_backup(self):
        result = self.run_dry("restore", "--disk", "disk9s4")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("restore backup", result.stdout)
        self.assertIn("verify SHA-256", result.stdout)


if __name__ == "__main__":
    unittest.main()
