import struct
import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "m1n1_windows" / "proxyclient"))

from m1n1.proxy import EVENT, Feature, UartInterface


class FakeSerial:
    def __init__(self, incoming):
        self.incoming = bytearray(incoming)

    def read(self, size):
        data = self.incoming[:size]
        del self.incoming[:size]
        return bytes(data)


class ProxyEventChecksumTest(unittest.TestCase):
    def test_accepts_valid_computed_event_checksum_after_fast_mode_reset(self):
        event = bytes.fromhex("ff55aa05050003006672616d65")
        event += struct.pack("<I", 0xC20B600E)
        reply = bytes.fromhex(
            "ff55aa01000000004f4b00000000000000000000000000000000000000000000"
        )
        reply += struct.pack("<I", 0xF39F09B5)

        iface = UartInterface.__new__(UartInterface)
        iface.dev = FakeSerial(event + reply)
        iface.debug = False
        iface.enabled_features = Feature.DISABLE_DATA_CSUMS
        iface.evt_handlers = {}
        received = []
        iface.set_event_handler(EVENT.FRAMEBUFFER, received.append)

        result = iface.reply(iface.REQ_PROXY)

        self.assertEqual(received, [b"frame"])
        self.assertEqual(result[:2], b"OK")

    def test_accepts_event_sentinel_when_local_feature_state_was_reset(self):
        event = bytes.fromhex("ff55aa05050003006672616d65")
        event += struct.pack("<I", UartInterface.CHECKSUM_SENTINEL)
        reply = bytes.fromhex(
            "ff55aa01000000004f4b00000000000000000000000000000000000000000000"
        )
        reply += struct.pack("<I", 0xF39F09B5)

        iface = UartInterface.__new__(UartInterface)
        iface.dev = FakeSerial(event + reply)
        iface.debug = False
        iface.enabled_features = Feature(0)
        iface.evt_handlers = {}
        received = []
        iface.set_event_handler(EVENT.FRAMEBUFFER, received.append)

        result = iface.reply(iface.REQ_PROXY)

        self.assertEqual(received, [b"frame"])
        self.assertEqual(result[:2], b"OK")

    def test_discards_corrupt_event_and_resynchronizes_to_next_event(self):
        corrupt = bytes.fromhex("ff55aa050600030062726f6b656e")
        corrupt += struct.pack("<I", 0)
        valid = bytes.fromhex("ff55aa05050003006672616d65")
        valid += struct.pack("<I", UartInterface.CHECKSUM_SENTINEL)
        reply = bytes.fromhex(
            "ff55aa01000000004f4b00000000000000000000000000000000000000000000"
        )
        reply += struct.pack("<I", 0xF39F09B5)

        iface = UartInterface.__new__(UartInterface)
        iface.dev = FakeSerial(corrupt + valid + reply)
        iface.debug = False
        iface.enabled_features = Feature.DISABLE_DATA_CSUMS
        iface.evt_handlers = {}
        received = []
        iface.set_event_handler(EVENT.FRAMEBUFFER, received.append)

        result = iface.reply(iface.REQ_PROXY)

        self.assertEqual(received, [b"frame"])
        self.assertEqual(result[:2], b"OK")


if __name__ == "__main__":
    unittest.main()
