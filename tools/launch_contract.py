#!/usr/bin/env python3
"""Strict decoder for framed J313 launch-contract snapshots."""

from __future__ import annotations

from dataclasses import dataclass
import argparse
import json
from pathlib import Path
import struct
import sys
import zlib


FRAME_MAGIC = b"J313CONTRACT"
FRAME_VERSION = 1
CONTRACT_MAGIC = 0x4A43314C
CONTRACT_VERSION = 1
MAX_REGIONS = 16
MAX_MAPPINGS = 64
MAX_CPUS = 8
MAX_IRQ_ROUTES = 16
REGIONS_OFFSET = 152
MAPPINGS_OFFSET = REGIONS_OFFSET + MAX_REGIONS * 24
CPUS_OFFSET = MAPPINGS_OFFSET + MAX_MAPPINGS * 32
IRQ_ROUTES_OFFSET = CPUS_OFFSET + MAX_CPUS * 72
DEVICES_OFFSET = IRQ_ROUTES_OFFSET + MAX_IRQ_ROUTES * 16
SNAPSHOT_SIZE = DEVICES_OFFSET + 64
FRAME = struct.Struct("<12sHHIIII")
SNAPSHOT_HEADER = struct.Struct("<IHHIIII")
COUNTS = struct.Struct("<IIII")


class ContractDecodeError(ValueError):
    pass


@dataclass(frozen=True)
class Snapshot:
    checkpoint: int
    sequence: int
    payload: bytes


class Decoder:
    def __init__(self) -> None:
        self._buffer = bytearray()
        self._seen: set[tuple[int, int]] = set()

    def feed(self, data: bytes) -> list[Snapshot]:
        self._buffer.extend(data)
        decoded: list[Snapshot] = []
        while len(self._buffer) >= FRAME.size:
            magic, version, header_size, payload_size, checkpoint, sequence, crc = FRAME.unpack_from(
                self._buffer
            )
            if magic != FRAME_MAGIC:
                raise ContractDecodeError("invalid frame magic")
            if version != FRAME_VERSION:
                raise ContractDecodeError("unsupported frame version")
            if header_size != FRAME.size:
                raise ContractDecodeError("invalid frame header size")
            if payload_size != SNAPSHOT_SIZE:
                raise ContractDecodeError("invalid payload length")
            record_size = header_size + payload_size
            if len(self._buffer) < record_size:
                break
            payload = bytes(self._buffer[header_size:record_size])
            del self._buffer[:record_size]
            if zlib.crc32(payload) != crc:
                raise ContractDecodeError("CRC mismatch")
            self._validate_snapshot(payload, checkpoint, sequence)
            key = (checkpoint, sequence)
            if key in self._seen:
                raise ContractDecodeError("duplicate checkpoint/sequence")
            self._seen.add(key)
            decoded.append(Snapshot(checkpoint, sequence, payload))
        return decoded

    def finish(self) -> None:
        if self._buffer:
            raise ContractDecodeError("trailing bytes")

    @staticmethod
    def _validate_snapshot(payload: bytes, checkpoint: int, sequence: int) -> None:
        magic, version, header_size, payload_size, inner_checkpoint, inner_sequence, crc = (
            SNAPSHOT_HEADER.unpack_from(payload)
        )
        if magic != CONTRACT_MAGIC or version != CONTRACT_VERSION:
            raise ContractDecodeError("unsupported contract version")
        if header_size != SNAPSHOT_HEADER.size or payload_size != len(payload) - header_size:
            raise ContractDecodeError("invalid contract length")
        if inner_checkpoint != checkpoint or inner_sequence != sequence:
            raise ContractDecodeError("frame/snapshot identity mismatch")
        if zlib.crc32(payload[header_size:]) != crc:
            raise ContractDecodeError("contract CRC mismatch")
        _, mapping_count, cpu_count, irq_count = COUNTS.unpack_from(payload, 96)
        if (mapping_count > MAX_MAPPINGS or cpu_count > MAX_CPUS or
                irq_count > MAX_IRQ_ROUTES):
            raise ContractDecodeError("contract count overflow")


def decode_records(data: bytes) -> list[Snapshot]:
    decoder = Decoder()
    records = decoder.feed(data)
    decoder.finish()
    return records


def encode_record(payload: bytes) -> bytes:
    """Frame one proxy-returned snapshot using the C transport ABI."""
    if len(payload) != SNAPSHOT_SIZE:
        raise ContractDecodeError("invalid payload length")
    _, _, _, _, checkpoint, sequence, _ = SNAPSHOT_HEADER.unpack_from(payload)
    Decoder._validate_snapshot(payload, checkpoint, sequence)
    return FRAME.pack(
        FRAME_MAGIC,
        FRAME_VERSION,
        FRAME.size,
        len(payload),
        checkpoint,
        sequence,
        zlib.crc32(payload),
    ) + payload


def _hex(value: int) -> str:
    return f"0x{value:x}"


def normalize(snapshot: Snapshot) -> dict:
    payload = snapshot.payload
    target, schema_revision, _ = struct.unpack_from("<IIQ", payload, 24)
    ram_base, ram_size, guest_entry, *args = struct.unpack_from("<7Q", payload, 40)
    region_count, mapping_count, cpu_count, irq_count = COUNTS.unpack_from(payload, 96)
    adt_size = struct.unpack_from("<Q", payload, 112)[0]
    adt_digest = payload[120:152].hex()

    regions = []
    for index in range(region_count):
        kind, flags, base, size = struct.unpack_from(
            "<IIQQ", payload, REGIONS_OFFSET + index * 24
        )
        regions.append({"kind": kind, "flags": flags, "base": _hex(base), "size": _hex(size)})

    mappings = []
    for index in range(mapping_count):
        ipa, pa, size, attributes = struct.unpack_from(
            "<4Q", payload, MAPPINGS_OFFSET + index * 32
        )
        mappings.append(
            {"ipa": _hex(ipa), "pa": _hex(pa), "size": _hex(size), "attributes": _hex(attributes)}
        )

    cpus = []
    cpu_fields = ("mpidr", "hacr", "mdcr", "mdscr", "amx_config", "apvmkeylo", "apvmkeyhi", "apsts", "actlr")
    for index in range(cpu_count):
        values = struct.unpack_from("<9Q", payload, CPUS_OFFSET + index * 72)
        cpus.append({name: _hex(value) for name, value in zip(cpu_fields, values)})

    irq_routes = []
    for index in range(irq_count):
        physical_irq, vintid, flags, device = struct.unpack_from(
            "<4I", payload, IRQ_ROUTES_OFFSET + index * 16
        )
        irq_routes.append(
            {"physical_irq": physical_irq, "vintid": vintid, "flags": flags, "device": device}
        )

    device_values = struct.unpack_from("<6Q4I", payload, DEVICES_OFFSET)
    devices = {
        "pci_ecam_base": _hex(device_values[0]),
        "nvme_bar_base": _hex(device_values[1]),
        "xhci_base": _hex(device_values[2]),
        "dart_base": _hex(device_values[3]),
        "vuart_base": _hex(device_values[4]),
        "display_base": _hex(device_values[5]),
        "display_width": device_values[6],
        "display_height": device_values[7],
        "display_stride": device_values[8],
        "flags": device_values[9],
    }
    return {
        "checkpoint": snapshot.checkpoint,
        "sequence": snapshot.sequence,
        "identity": {"target": "J313" if target == 0x3331334A else _hex(target),
                     "schema_revision": schema_revision},
        "boot": {"ram_base": _hex(ram_base), "ram_size": _hex(ram_size),
                 "guest_entry": _hex(guest_entry), "args": [_hex(value) for value in args]},
        "adt": {"size": _hex(adt_size), "sha256": adt_digest},
        "regions": regions,
        "mappings": mappings,
        "cpus": cpus,
        "irq_routes": irq_routes,
        "devices": devices,
    }


def compare(expected: object, actual: object, path: str = "") -> list[str]:
    if isinstance(expected, dict) and isinstance(actual, dict):
        differences: list[str] = []
        for key in sorted(set(expected) | set(actual)):
            child = f"{path}.{key}" if path else key
            if key not in expected:
                differences.append(f"{child}: unexpected {actual[key]}")
            elif key not in actual:
                differences.append(f"{child}: missing, expected {expected[key]}")
            else:
                differences.extend(compare(expected[key], actual[key], child))
        return differences
    if isinstance(expected, list) and isinstance(actual, list):
        differences = []
        for index in range(max(len(expected), len(actual))):
            child = f"{path}[{index}]"
            if index >= len(expected):
                differences.append(f"{child}: unexpected {actual[index]}")
            elif index >= len(actual):
                differences.append(f"{child}: missing, expected {expected[index]}")
            else:
                differences.extend(compare(expected[index], actual[index], child))
        return differences
    if expected != actual:
        return [f"{path}: expected {expected}, actual {actual}"]
    return []


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    decode_parser = subparsers.add_parser("decode")
    decode_parser.add_argument("capture", type=Path)
    decode_parser.add_argument("--output", type=Path, required=True)
    compare_parser = subparsers.add_parser("compare")
    compare_parser.add_argument("expected", type=Path)
    compare_parser.add_argument("actual", type=Path)
    args = parser.parse_args(argv)

    if args.command == "decode":
        normalized = [normalize(item) for item in decode_records(args.capture.read_bytes())]
        args.output.write_text(json.dumps(normalized, indent=2, sort_keys=True) + "\n")
        return 0

    expected = json.loads(args.expected.read_text())
    actual = json.loads(args.actual.read_text())
    differences = compare(expected, actual)
    if differences:
        print("\n".join(differences))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
