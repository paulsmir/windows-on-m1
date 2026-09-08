#!/usr/bin/env python3
import argparse
import json
import struct
from pathlib import Path


STATE_VERSION = 2
CAPACITY = 2
HEADER = struct.Struct("<12I")
SLOT = struct.Struct("<26I9Q")
STATE_BYTES = HEADER.size + CAPACITY * SLOT.size

HEADER_NAMES = (
    "version", "bytes", "candidate_build", "boot_generation", "count",
    "overflow", "captured_generation", "exported_generation",
    "export_attempted", "export_status", "durable", "reserved",
)
SLOT32_NAMES = (
    "version", "bytes", "candidate_build", "boot_generation",
    "call_sequence", "valid_mask", "render_guard", "render_status",
    "command_length", "allocation_count", "destination_index",
    "destination_segment", "dma_bytes_produced", "patches_produced",
    "prepatched", "patch_guard", "patch_status", "submit_guard",
    "submit_status", "fence", "worker_status", "reserved",
    "synchronize_status", "queue_dpc_result", "query_fence_count",
    "query_fence_value",
)
SLOT64_NAMES = (
    "entry_timestamp", "exit_timestamp", "notify_timestamp", "dpc_timestamp",
    "adapter_token", "context_token",
    "command_hash", "allocation_token_0", "allocation_token_1",
)


def decode_bytes(data: bytes) -> dict:
    if len(data) != STATE_BYTES:
        raise ValueError(f"state size {len(data)} != {STATE_BYTES}")
    header = dict(zip(HEADER_NAMES, HEADER.unpack_from(data, 0)))
    if header["version"] != STATE_VERSION or header["bytes"] != STATE_BYTES:
        raise ValueError("state identity")
    if not 0 <= header["count"] <= CAPACITY:
        raise ValueError("state count")
    slots = []
    offset = HEADER.size
    for index in range(CAPACITY):
        values = SLOT.unpack_from(data, offset)
        offset += SLOT.size
        slot = dict(zip(SLOT32_NAMES, values[:26]))
        slot.update(zip(SLOT64_NAMES, values[26:]))
        if index < header["count"]:
            if (slot["version"] != STATE_VERSION or
                    slot["bytes"] != SLOT.size or
                    slot["candidate_build"] != header["candidate_build"] or
                    slot["boot_generation"] != header["boot_generation"] or
                    slot["call_sequence"] != index + 1):
                raise ValueError(f"slot {index + 1} identity")
            slots.append(slot)
    header["slots"] = slots
    return header


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("receipt", type=Path)
    args = parser.parse_args()
    print(json.dumps(decode_bytes(args.receipt.read_bytes()), indent=2,
                     sort_keys=True))


if __name__ == "__main__":
    main()
