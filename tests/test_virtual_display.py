import json
from pathlib import Path
import struct
import tempfile
import unittest

from virtual_display import (
    FB_STREAM_MAGIC,
    FrameReceiver,
    VirtualDisplayConfig,
)


HEADER = struct.Struct("<IIIIHHII")


def chunk(config, frame_id, offset, payload, *, magic=FB_STREAM_MAGIC,
          total_size=None, width=None, height=None, stride=None,
          payload_size=None):
    return HEADER.pack(
        magic,
        frame_id,
        offset,
        config.size if total_size is None else total_size,
        config.width if width is None else width,
        config.height if height is None else height,
        config.stride if stride is None else stride,
        len(payload) if payload_size is None else payload_size,
    ) + payload


class TestVirtualDisplayConfig(unittest.TestCase):
    def test_rejects_outside_guest_ram_and_named_overlaps(self):
        config = VirtualDisplayConfig(base=0x5000, width=8, height=2, stride=32)

        config.validate((0x1000, 0x10000), {"firmware": (0x1000, 0x4000)})
        with self.assertRaisesRegex(ValueError, "guest RAM"):
            config.validate((0x6000, 0x10000), {})

        for name, window in {
            "firmware": (0x4FFF, 0x5001),
            "ramdisk": (0x503F, 0x9000),
            "low-memory backing": (0x4000, 0x5040),
        }.items():
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, name):
                config.validate((0x1000, 0x10000), {name: window})

    def test_half_open_adjacent_ranges_do_not_overlap(self):
        config = VirtualDisplayConfig(base=0x5000, width=8, height=2, stride=32)
        config.validate(
            (0x1000, 0x10000),
            {"before": (0x1000, 0x5000), "after": (0x5040, 0x10000)},
        )


class TestFrameReceiver(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        root = Path(self.tempdir.name)
        self.raw = root / "fb.raw"
        self.info = root / "fb-info.json"
        self.config = VirtualDisplayConfig(base=0x5000, width=8, height=2, stride=32)
        self.receiver = FrameReceiver(self.config, self.raw, self.info)
        self.frame = bytes(range(64))

    def publish_frame(self, frame_id=1):
        self.receiver.accept(chunk(self.config, frame_id, 0, self.frame[:7]))
        self.receiver.accept(chunk(self.config, frame_id, 7, self.frame[7:41]))
        self.receiver.accept(chunk(self.config, frame_id, 41, self.frame[41:]))

    def test_ordered_chunks_publish_only_when_complete(self):
        self.receiver.accept(chunk(self.config, 1, 0, self.frame[:7]))
        self.assertFalse(self.raw.exists())
        self.receiver.accept(chunk(self.config, 1, 7, self.frame[7:41]))
        self.assertFalse(self.raw.exists())
        self.receiver.accept(chunk(self.config, 1, 41, self.frame[41:]))

        self.assertEqual(self.raw.read_bytes(), self.frame)
        metadata = json.loads(self.info.read_text())
        self.assertEqual(metadata["generation"], 1)
        self.assertEqual(metadata["frame_id"], 1)

    def test_invalid_sequences_retain_last_good_frame(self):
        self.publish_frame(1)
        last_info = self.info.read_bytes()

        invalid_sequences = [
            [chunk(self.config, 2, 0, b"abc"), chunk(self.config, 2, 4, b"def")],
            [chunk(self.config, 2, 0, b"abc"), chunk(self.config, 2, 0, b"abc")],
            [chunk(self.config, 2, 0, b"abc"), chunk(self.config, 2, 2, b"def")],
            [chunk(self.config, 2, 0, b"abc", payload_size=4)],
            [chunk(self.config, 2, 0, b"abc", magic=0)],
            [chunk(self.config, 2, 0, b"abc", width=9)],
        ]
        for sequence in invalid_sequences:
            with self.subTest(sequence=sequence):
                for data in sequence:
                    self.receiver.accept(data)
                self.assertEqual(self.raw.read_bytes(), self.frame)
                self.assertEqual(self.info.read_bytes(), last_info)

    def test_offset_zero_recovers_after_partial_frame(self):
        self.receiver.accept(chunk(self.config, 4, 0, self.frame[:13]))
        replacement = bytes(reversed(self.frame))
        self.receiver.accept(chunk(self.config, 5, 0, replacement[:31]))
        self.receiver.accept(chunk(self.config, 5, 31, replacement[31:]))
        self.assertEqual(self.raw.read_bytes(), replacement)
        self.assertEqual(json.loads(self.info.read_text())["generation"], 1)

    def test_generation_changes_only_after_complete_publication(self):
        self.publish_frame(7)
        self.receiver.accept(chunk(self.config, 8, 0, self.frame[:32]))
        self.assertEqual(json.loads(self.info.read_text())["generation"], 1)
        self.receiver.accept(chunk(self.config, 8, 32, self.frame[32:]))
        self.assertEqual(json.loads(self.info.read_text())["generation"], 2)


if __name__ == "__main__":
    unittest.main()
