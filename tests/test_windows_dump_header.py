import struct
import tempfile
from pathlib import Path
import unittest

from tools.windows_dump_header import DumpFormatError, parse_dump_header


def synthetic_dump(*, machine=0xAA64, processors=8, code=0x101,
                   parameters=(0x18, 0, 0xFFFF9880C46F1980, 1)):
    data = bytearray(0x2000)
    data[0:8] = b"PAGEDU64"
    struct.pack_into("<I", data, 0x30, machine)
    struct.pack_into("<I", data, 0x34, processors)
    struct.pack_into("<I", data, 0x38, code)
    struct.pack_into("<4Q", data, 0x40, *parameters)
    return bytes(data)


class WindowsDumpHeaderTests(unittest.TestCase):
    def test_parses_arm64_clock_watchdog_fields(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "memory.dmp"
            path.write_bytes(synthetic_dump())
            header = parse_dump_header(path)
        self.assertEqual(header.machine, "arm64")
        self.assertEqual(header.processor_count, 8)
        self.assertEqual(header.bugcheck_code, 0x101)
        self.assertEqual(header.parameters, (0x18, 0, 0xFFFF9880C46F1980, 1))
        self.assertEqual(header.hung_cpu, 1)
        self.assertEqual(header.hung_prcb, 0xFFFF9880C46F1980)

    def test_preserves_unknown_bugcheck_without_watchdog_interpretation(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "memory.dmp"
            path.write_bytes(synthetic_dump(code=0x7E, parameters=(0xC0000005, 2, 3, 4)))
            header = parse_dump_header(path)
        self.assertIsNone(header.hung_cpu)
        self.assertIsNone(header.hung_prcb)

    def test_rejects_non_dump_and_non_arm64_headers(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.dmp"
            path.write_bytes(b"not a dump")
            with self.assertRaises(DumpFormatError):
                parse_dump_header(path)
            path.write_bytes(synthetic_dump(machine=0x8664))
            with self.assertRaises(DumpFormatError):
                parse_dump_header(path)


if __name__ == "__main__":
    unittest.main()
