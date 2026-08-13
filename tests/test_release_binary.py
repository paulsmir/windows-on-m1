import tempfile
import unittest
from pathlib import Path

from tools.check_release_binary import check


class ReleaseBinaryTests(unittest.TestCase):
    def test_accepts_quiet_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "m1n1.macho"
            path.write_bytes(b"fatal boot failure\0")
            check(path)

    def test_rejects_each_periodic_diagnostic_family(self):
        for diagnostic in (
            b"HV FIQ:",
            b"HV TIMER:",
            b"HV SGI DIAG:",
            b"HV WATCHDOG PERIODIC:",
            b"FW> ",
            b"HV SGI QUEUE:",
            b"HV PMUv3 Redirect:",
            b"HV: PCI cfg 00:00.0",
            b"HV: NVMe SQ q=",
        ):
            with self.subTest(diagnostic=diagnostic):
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory) / "m1n1.macho"
                    path.write_bytes(b"prefix\0" + diagnostic + b" suffix")
                    with self.assertRaises(ValueError):
                        check(path)


if __name__ == "__main__":
    unittest.main()
