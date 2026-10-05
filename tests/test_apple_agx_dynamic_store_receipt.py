from pathlib import Path
import importlib.util
import struct
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
DECODER = ROOT / "tools" / "decode_apple_agx_dynamic_store.py"


def load_decoder():
    spec = importlib.util.spec_from_file_location("dynamic_store_decoder", DECODER)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class AppleAgxDynamicStoreReceiptTests(unittest.TestCase):
    def receipt(self):
        words32 = [1, 248, 1, 271, 0x06640001, 0x4000,
                   0x24004, 0x24004, 0x24004, 0]
        words64 = [
            0x1500FA0000, 0x9BCFD0000, 0x1500FA0000,
            0x14000000, 0x22004, 0x23004, 0x23004,
            0xB4F04EB2FD87523D, 0xE0CCBE718205F3CB,
            0x30C354D48249BB26, 0x1503920000400C1D,
            0x15039230001000DD, 0x150392400040041D,
            0x1100010400, 0xC3387EBEA1B9F34D,
            0x1503950000, 0x000003C00FC60A22,
            0x10000001500FA000, 0,
            0x1111111111111111, 0x1503960000,
            0xFFFFFFFF00000000, 0, 0, 0,
            0x2222222222222222,
        ]
        return struct.pack("<10I26Q", *words32, *words64)

    def test_decodes_exact_active_store_contract(self):
        decoder = load_decoder()
        decoded = decoder.decode_receipt(self.receipt())
        self.assertEqual(decoded["version"], 1)
        self.assertEqual(decoded["bytes"], 248)
        self.assertEqual(decoded["fence"], 271)
        self.assertEqual(decoded["generation"], 0x06640001)
        self.assertEqual(decoded["destination_gpu_va"], "0x1500fa0000")
        self.assertEqual(decoded["attachment_gpu_va"], "0x1500fa0000")
        self.assertEqual(decoded["work"], {
            "pipeline_base_raw": "0x14000000",
            "load": "0x22004",
            "store": "0x24004",
            "reload0": "0x23004",
            "reload1": "0x23004",
            "partial_store0": "0x24004",
            "partial_store1": "0x24004",
        })
        self.assertEqual(decoded["store_shader_gpu_va"], "0x1100010400")
        self.assertEqual(decoded["render_target"]["qword0"],
                         "0x3c00fc60a22")
        self.assertEqual(decoded["render_target"]["qword1"],
                         "0x10000001500fa000")
        self.assertEqual(decoded["companion"]["qword0"],
                         "0xffffffff00000000")

    def test_rejects_wrong_size_version_or_invalid_record(self):
        decoder = load_decoder()
        raw = bytearray(self.receipt())
        with self.assertRaisesRegex(ValueError, "248 bytes"):
            decoder.decode_receipt(raw[:-1])
        struct.pack_into("<I", raw, 0, 2)
        with self.assertRaisesRegex(ValueError, "version"):
            decoder.decode_receipt(raw)
        struct.pack_into("<I", raw, 0, 1)
        struct.pack_into("<I", raw, 8, 0)
        with self.assertRaisesRegex(ValueError, "valid"):
            decoder.decode_receipt(raw)

    def test_cli_writes_machine_readable_json(self):
        decoder = load_decoder()
        with tempfile.TemporaryDirectory() as directory:
            raw = Path(directory) / "store.bin"
            raw.write_bytes(self.receipt())
            decoded = decoder.decode_path(raw)
            self.assertEqual(decoded["fence"], 271)


if __name__ == "__main__":
    unittest.main()
