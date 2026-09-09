from pathlib import Path
import importlib.util
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
DECODER = ROOT / "tools" / "decode_apple_agx_dynamic_output.py"


def load_decoder():
    spec = importlib.util.spec_from_file_location("dynamic_output_decoder", DECODER)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def fnv1a(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


class AppleAgxDynamicOutputSnapshotTests(unittest.TestCase):
    def receipt(self):
        data = bytes((index * 17 + 3) & 0xFF for index in range(1024))
        header = struct.pack(
            "<8I3Q",
            1,
            1080,
            1,
            271,
            7,
            len(data),
            0,
            0,
            0x1500FA0000,
            0x9BD140000,
            fnv1a(data),
        )
        return header + data

    def test_decodes_and_validates_exact_snapshot(self):
        decoder = load_decoder()
        decoded = decoder.decode_receipt(self.receipt())
        self.assertEqual(decoded["version"], 1)
        self.assertEqual(decoded["bytes"], 1080)
        self.assertEqual(decoded["fence"], 271)
        self.assertEqual(decoded["generation"], 7)
        self.assertEqual(decoded["data_bytes"], 1024)
        self.assertEqual(decoded["source_gpu_va"], "0x1500fa0000")
        self.assertEqual(decoded["source_physical"], "0x9bd140000")
        self.assertEqual(decoded["fnv1a"], hex(fnv1a(self.receipt()[56:])))

    def test_rejects_wrong_size_identity_or_hash(self):
        decoder = load_decoder()
        raw = bytearray(self.receipt())
        with self.assertRaisesRegex(ValueError, "1080 bytes"):
            decoder.decode_receipt(raw[:-1])
        struct.pack_into("<I", raw, 0, 2)
        with self.assertRaisesRegex(ValueError, "version"):
            decoder.decode_receipt(raw)
        struct.pack_into("<I", raw, 0, 1)
        raw[-1] ^= 1
        with self.assertRaisesRegex(ValueError, "hash"):
            decoder.decode_receipt(raw)


if __name__ == "__main__":
    unittest.main()
