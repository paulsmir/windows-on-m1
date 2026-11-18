from pathlib import Path
import struct
import unittest
import zlib

from tools.launch_contract import (
    ContractDecodeError,
    Decoder,
    LAYOUTS,
    MAPPINGS_OFFSET,
    SNAPSHOT_SIZE_V1,
    SNAPSHOT_SIZE_V2,
    compare,
    decode_records,
    encode_record,
    normalize,
)


FRAME = struct.Struct("<12sHHIIII")
SNAPSHOT_HEADER = struct.Struct("<IHHIIII")
MAGIC = b"J313CONTRACT"


FIXTURE_V1 = Path(__file__).resolve().parent / "fixtures/launch_contract_exp2693_cp2_v1.bin"


def snapshot(checkpoint: int, sequence: int, version: int = 1) -> bytes:
    payload_size = LAYOUTS[version].size - SNAPSHOT_HEADER.size
    body = bytes(payload_size)
    header = SNAPSHOT_HEADER.pack(
        0x4A43314C,
        version,
        SNAPSHOT_HEADER.size,
        payload_size,
        checkpoint,
        sequence,
        zlib.crc32(body),
    )
    return header + body


def seal(payload: bytearray) -> bytes:
    struct.pack_into("<I", payload, 20, zlib.crc32(bytes(payload[24:])))
    return bytes(payload)


def frame(payload: bytes, checkpoint: int, sequence: int) -> bytes:
    return FRAME.pack(MAGIC, 1, FRAME.size, len(payload), checkpoint, sequence,
                      zlib.crc32(payload)) + payload


def repack(payload: bytes, version: int) -> bytes:
    """The same machine state in another record version (only counted entries move)."""
    src = LAYOUTS[struct.unpack_from("<H", payload, 4)[0]]
    dst = LAYOUTS[version]
    regions, mappings, cpus, routes = struct.unpack_from("<IIII", payload, 96)
    out = bytearray(dst.size)
    out[:MAPPINGS_OFFSET] = payload[:MAPPINGS_OFFSET]
    struct.pack_into("<HHI", out, 4, version, SNAPSHOT_HEADER.size, dst.size - SNAPSHOT_HEADER.size)
    out[MAPPINGS_OFFSET:MAPPINGS_OFFSET + mappings * 32] = \
        payload[MAPPINGS_OFFSET:MAPPINGS_OFFSET + mappings * 32]
    out[dst.cpus_offset:dst.cpus_offset + 8 * 72] = payload[src.cpus_offset:src.cpus_offset + 8 * 72]
    out[dst.irq_routes_offset:dst.irq_routes_offset + routes * 16] = \
        payload[src.irq_routes_offset:src.irq_routes_offset + routes * 16]
    out[dst.devices_offset:dst.devices_offset + 64] = payload[src.devices_offset:src.devices_offset + 64]
    return seal(out)


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
        struct.pack_into("<QQQQ", payload, MAPPINGS_OFFSET,
                         0x100000, 0x8A0100000, 0x200000, 1)
        struct.pack_into("<I", payload, 20, zlib.crc32(payload[24:]))
        frame = FRAME.pack(MAGIC, 1, FRAME.size, len(payload), 3, 4, zlib.crc32(payload)) + payload
        item = Decoder().feed(frame)[0]
        value = normalize(item)
        self.assertEqual(value["identity"]["target"], "J313")
        self.assertEqual(value["boot"]["guest_entry"], "0x8510b4000")
        self.assertEqual(value["mappings"][0]["pa"], "0x8a0100000")

    def test_accepts_the_full_assisted_stage2_operation_set(self):
        payload = bytearray(snapshot(2, 3))
        struct.pack_into("<IIII", payload, 96, 0, 33, 0, 0)
        for index in range(33):
            struct.pack_into(
                "<4Q", payload, MAPPINGS_OFFSET + index * 32,
                0x200000000 + index * 0x4000,
                0x200000000 + index * 0x4000,
                0x4000,
                1,
            )
        struct.pack_into("<I", payload, 20, zlib.crc32(payload[24:]))
        frame = FRAME.pack(MAGIC, 1, FRAME.size, len(payload), 2, 3, zlib.crc32(payload)) + payload

        value = normalize(Decoder().feed(frame)[0])

        self.assertEqual(len(value["mappings"]), 33)
        self.assertEqual(value["mappings"][-1]["ipa"], "0x200080000")

    # Contract v2 (m1n1-standing-profile.md section 3): v1 and v2 records, fail closed otherwise.

    def test_record_sizes_are_the_c_abi(self):
        self.assertEqual((SNAPSHOT_SIZE_V1, SNAPSHOT_SIZE_V2), (3480, 5784))

    def test_real_v1_record_and_its_v2_repack_normalize_equal(self):
        v1 = FIXTURE_V1.read_bytes()  # EXP2693 checkpoint 2, ADT digest zeroed
        self.assertEqual(len(v1), SNAPSHOT_SIZE_V1)
        v2 = repack(v1, 2)
        a = normalize(decode_records(frame(v1, 2, 3))[0])
        b = normalize(decode_records(frame(v2, 2, 3))[0])
        self.assertEqual(len(a["mappings"]), 61)
        self.assertEqual(len(a["irq_routes"]), 13)
        self.assertEqual(a, b)
        self.assertEqual(compare(a, b), [])
        self.assertEqual(decode_records(encode_record(v2))[0].payload, v2)

    def test_v1_only_decoder_refuses_a_v2_record(self):
        v2 = repack(FIXTURE_V1.read_bytes(), 2)
        with self.assertRaisesRegex(ContractDecodeError, "invalid payload length"):
            Decoder(accepted_versions={1}).feed(frame(v2, 2, 3))
        # and a v1 record still decodes there (images built before v2)
        self.assertEqual(len(Decoder(accepted_versions={1}).feed(frame(FIXTURE_V1.read_bytes(), 2, 3))), 1)

    def test_refuses_a_size_that_is_neither_v1_nor_v2(self):
        for size in (0, 3479, 3481, 4000, 5783, 5785):
            payload = bytes(size)
            with self.assertRaisesRegex(ContractDecodeError, "invalid payload length"):
                Decoder().feed(frame(payload, 0, 1))
            with self.assertRaisesRegex(ContractDecodeError, "invalid payload length"):
                encode_record(payload)

    def test_refuses_a_header_version_that_disagrees_with_the_size(self):
        v1 = bytearray(snapshot(0, 1, 1))
        struct.pack_into("<H", v1, 4, 2)  # says v2, has the v1 size
        v2 = bytearray(snapshot(0, 1, 2))
        struct.pack_into("<H", v2, 4, 1)  # says v1, has the v2 size
        for payload in (seal(v1), seal(v2)):
            with self.assertRaisesRegex(ContractDecodeError, "version/size mismatch"):
                Decoder().feed(frame(payload, 0, 1))
            with self.assertRaisesRegex(ContractDecodeError, "version/size mismatch"):
                encode_record(payload)

    def test_refuses_an_unknown_version(self):
        payload = bytearray(snapshot(0, 1, 2))
        struct.pack_into("<H", payload, 4, 3)
        with self.assertRaisesRegex(ContractDecodeError, "unsupported contract version"):
            Decoder().feed(frame(seal(payload), 0, 1))
        with self.assertRaises(ValueError):
            Decoder(accepted_versions={3})

    def test_count_limits_follow_the_version(self):
        def with_counts(version, mappings, routes):
            payload = bytearray(snapshot(2, 3, version))
            struct.pack_into("<IIII", payload, 96, 0, mappings, 0, routes)
            return frame(seal(payload), 2, 3)
        self.assertEqual(len(Decoder().feed(with_counts(1, 64, 16))), 1)
        with self.assertRaisesRegex(ContractDecodeError, "count overflow"):
            Decoder().feed(with_counts(1, 65, 16))
        with self.assertRaisesRegex(ContractDecodeError, "count overflow"):
            Decoder().feed(with_counts(1, 64, 17))
        self.assertEqual(len(Decoder().feed(with_counts(2, 128, 32))), 1)
        with self.assertRaisesRegex(ContractDecodeError, "count overflow"):
            Decoder().feed(with_counts(2, 129, 32))
        with self.assertRaisesRegex(ContractDecodeError, "count overflow"):
            Decoder().feed(with_counts(2, 128, 33))

    def test_compare_returns_stable_field_paths(self):
        expected = {"boot": {"guest_entry": "0x1000"}, "cpus": [{"mpidr": "0x0"}]}
        actual = {"boot": {"guest_entry": "0x2000"}, "cpus": [{"mpidr": "0x0"}]}
        self.assertEqual(compare(expected, actual), ["boot.guest_entry: expected 0x1000, actual 0x2000"])


if __name__ == "__main__":
    unittest.main()
