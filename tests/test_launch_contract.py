import struct
import unittest
import zlib

from tools.launch_contract import (
    ContractDecodeError,
    Decoder,
    SNAPSHOT_SIZE,
    compare,
    decode_records,
    encode_record,
    normalize,
)


FRAME = struct.Struct("<12sHHIIII")
SNAPSHOT_HEADER = struct.Struct("<IHHIIII")
MAGIC = b"J313CONTRACT"


def snapshot(checkpoint: int, sequence: int) -> bytes:
    payload_size = SNAPSHOT_SIZE - SNAPSHOT_HEADER.size
    body = bytes(payload_size)
    header = SNAPSHOT_HEADER.pack(
        0x4A43314C,
        1,
        SNAPSHOT_HEADER.size,
        payload_size,
        checkpoint,
        sequence,
        zlib.crc32(body),
    )
    return header + body


def record(checkpoint: int, sequence: int) -> bytes:
    payload = snapshot(checkpoint, sequence)
    return FRAME.pack(
        MAGIC,
        1,
        FRAME.size,
        len(payload),
        checkpoint,
        sequence,
        zlib.crc32(payload),
    ) + payload


class LaunchContractDecoderTests(unittest.TestCase):
    def test_decodes_two_records_from_uneven_chunks(self):
        stream = record(0, 1) + record(3, 4)
        decoder = Decoder()
        decoded = []
        for offset in range(0, len(stream), 13):
            decoded.extend(decoder.feed(stream[offset : offset + 13]))
        decoder.finish()
        self.assertEqual([(item.checkpoint, item.sequence) for item in decoded], [(0, 1), (3, 4)])

    def test_host_encoder_matches_decoder(self):
        payload = snapshot(3, 4)
        decoded = decode_records(encode_record(payload))
        self.assertEqual([(item.checkpoint, item.sequence) for item in decoded], [(3, 4)])

    def test_rejects_corrupt_payload(self):
        stream = bytearray(record(3, 4))
        stream[-1] ^= 1
        decoder = Decoder()
        with self.assertRaisesRegex(ContractDecodeError, "CRC mismatch"):
            decoder.feed(stream)

    def test_rejects_duplicate_checkpoint_sequence(self):
        decoder = Decoder()
        with self.assertRaisesRegex(ContractDecodeError, "duplicate checkpoint/sequence"):
            decoder.feed(record(0, 1) + record(0, 1))

    def test_normalizes_boot_and_mapping_fields(self):
        payload = bytearray(snapshot(3, 4))
        struct.pack_into("<IIQ", payload, 24, 0x3331334A, 1, 0)
        struct.pack_into("<QQQQQQQ", payload, 40, 0x800000000, 0x200000000, 0x8510B4000,
                         0x854000000, 0, 0, 0)
        struct.pack_into("<IIII", payload, 96, 0, 1, 0, 0)
        struct.pack_into("<QQQQ", payload, 536, 0x100000, 0x8A0100000, 0x200000, 1)
        struct.pack_into("<I", payload, 20, zlib.crc32(payload[24:]))
        frame = FRAME.pack(MAGIC, 1, FRAME.size, len(payload), 3, 4, zlib.crc32(payload)) + payload
        item = Decoder().feed(frame)[0]
        value = normalize(item)
        self.assertEqual(value["identity"]["target"], "J313")
        self.assertEqual(value["boot"]["guest_entry"], "0x8510b4000")
        self.assertEqual(value["mappings"][0]["pa"], "0x8a0100000")

    def test_compare_returns_stable_field_paths(self):
        expected = {"boot": {"guest_entry": "0x1000"}, "cpus": [{"mpidr": "0x0"}]}
        actual = {"boot": {"guest_entry": "0x2000"}, "cpus": [{"mpidr": "0x0"}]}
        self.assertEqual(compare(expected, actual), ["boot.guest_entry: expected 0x1000, actual 0x2000"])


if __name__ == "__main__":
    unittest.main()
