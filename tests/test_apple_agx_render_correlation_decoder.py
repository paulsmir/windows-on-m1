from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
DECODER = ROOT / "drivers/apple-agx/render-admission/scripts/decode-render-correlation.py"


def load_decoder():
    spec = spec_from_file_location("render_correlation_decoder", DECODER)
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class RenderCorrelationDecoderTests(unittest.TestCase):
    def test_two_calls_remain_bound_to_one_candidate_and_boot(self):
        decoder = load_decoder()
        header = (1, decoder.STATE_BYTES, 612, 77, 2, 0, 9, 9, 1, 0, 1, 0)
        slots = []
        for sequence, context, mask in ((1, 0x1111, 0x1FF), (2, 0x2222, 1)):
            words = [
                1, decoder.SLOT.size, 612, 77, sequence, mask, 0, 0,
                48, 2, sequence - 1, 2, 168 if sequence == 1 else 0,
                1 if sequence == 1 else 0, 1 if sequence == 1 else 0,
                0, 0, 0, 0, 255 + sequence, 0, 0,
            ]
            qwords = [100 * sequence, 110 * sequence, 0xAAAA, context,
                      0xABC000 + sequence, 0x1000, 0x2000]
            slots.append(decoder.SLOT.pack(*(words + qwords)))
        decoded = decoder.decode_bytes(
            decoder.HEADER.pack(*header) + b"".join(slots)
        )
        self.assertEqual(decoded["candidate_build"], 612)
        self.assertEqual(decoded["boot_generation"], 77)
        self.assertEqual([s["context_token"] for s in decoded["slots"]],
                         [0x1111, 0x2222])
        self.assertEqual(decoded["slots"][1]["valid_mask"], 1)

    def test_rejects_slot_from_another_boot(self):
        decoder = load_decoder()
        header = (1, decoder.STATE_BYTES, 612, 77, 1, 0, 1, 0, 0, 0, 0, 0)
        words = [1, decoder.SLOT.size, 612, 78, 1, 1] + [0] * 16
        slot = decoder.SLOT.pack(*(words + [1, 0, 1, 2, 3, 4, 5]))
        empty = bytes(decoder.SLOT.size)
        with self.assertRaisesRegex(ValueError, "slot 1 identity"):
            decoder.decode_bytes(decoder.HEADER.pack(*header) + slot + empty)


if __name__ == "__main__":
    unittest.main()
