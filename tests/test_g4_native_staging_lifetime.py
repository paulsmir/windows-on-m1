"""Execute the real Windows native-staging owner with mocked kernel services.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A native ring must never be
freed while a user view or WDDM allocation binding can reach its pages. Exit
and foreign-process unmap must serialize; allocation failures must undo both
page ownership and pool charge; authenticated bindings must use exact PFNs.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers/apple-agx/render-admission"


class NativeStagingLifetime(unittest.TestCase):
    def test_real_manager_c_and_cpp(self):
        source = RENDER / "src/native_staging_windows.c"
        self.assertTrue(source.exists(), "native staging lifetime owner is absent")
        with tempfile.TemporaryDirectory(prefix="native-staging-") as path:
            tmp = Path(path)
            shutil.copyfile(source, tmp / source.name)
            (tmp / "render_admission.h").write_text("")
            (tmp / "ntifs.h").write_text("")
            for language, standard in (("c", "c11"), ("c++", "c++17")):
                with self.subTest(language=language):
                    exe = tmp / ("owner-" + language.replace("+", "p"))
                    command = ["clang", "-x", language, "-std=" + standard,
                               "-Wall", "-Wextra", "-Werror", "-pthread",
                               "-fsanitize=address,undefined", "-g",
                               "-I", str(tmp), "-I", str(RENDER / "include"),
                               str(ROOT / "tests/native_staging_windows_test.c"),
                               "-o", str(exe)]
                    built = subprocess.run(command, capture_output=True, text=True)
                    self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                    for scenario in ("normal", "unload-live"):
                        with self.subTest(language=language, scenario=scenario):
                            run = subprocess.run([str(exe), scenario], capture_output=True,
                                                 text=True, timeout=45)
                            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                            self.assertIn("native staging lifetime PASS", run.stdout)


if __name__ == "__main__":
    unittest.main()
