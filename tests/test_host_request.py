import pathlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from tools import host_request  # noqa: E402


class FakeHeap:
    def malloc(self, size):
        return 0x1000

    def free(self, address):
        pass


class FakeIface:
    def readmem(self, address, size):
        return struct.pack("<QQIIIIII", 2, 0, 1, 3, 4, 0, 0, 77)


class FakeProxy:
    def __init__(self):
        self.calls = []
        self.heap = FakeHeap()
        self.iface = FakeIface()

    def request(self, op, *args, **kwargs):
        self.calls.append((op, args))
        return 0 if op != host_request.P_DISPLAY_SCANOUT_RECOVERY_STATUS else 1


class HostRequestTests(unittest.TestCase):
    def test_request_file_is_consumed_once(self):
        proxy = FakeProxy()
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "req.txt"
            path.write_text("refuse 1\n# comment\ndrop 2\nstatus\n")
            lines = host_request.consume(path, proxy)
            self.assertEqual([c[0] for c in proxy.calls],
                             [0x110B, 0x110C, 0x110D])
            self.assertEqual(proxy.calls[0][1], (1,))
            self.assertIn("abandoned_swap_id=77", lines[2])
            self.assertFalse(path.exists())
            again = host_request.consume(path, proxy)
            self.assertEqual(again, ["HOST_REQUEST none (no request file)"])
            self.assertEqual(len(proxy.calls), 3)

    def test_malformed_file_arms_nothing(self):
        proxy = FakeProxy()
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "req.txt"
            path.write_text("refuse 1\nrefuse lots\n")
            lines = host_request.consume(path, proxy)
            self.assertTrue(lines[0].startswith("HOST_REQUEST rejected"))
            self.assertEqual(proxy.calls, [])

    def test_count_is_bounded(self):
        with self.assertRaises(ValueError):
            host_request.parse(f"drop {host_request.MAX_COUNT + 1}")


if __name__ == "__main__":
    unittest.main()
