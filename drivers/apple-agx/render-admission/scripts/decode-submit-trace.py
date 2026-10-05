#!/usr/bin/env python3
"""Decode EXP507 submit qualification words from a durable m1n1 host log."""

import argparse
import json
import re
from pathlib import Path


TAG = 0x5070
LINE = re.compile(
    r"AGX power receipt seq=(0x[0-9a-fA-F]+|[0-9]+) "
    r"cmd=([0-9]+) state=([0-9]+) result=([0-9]+)"
)
NAMES = {
    1: "version", 2: "irql", 5: "flags", 6: "node", 7: "engine",
    8: "fence", 11: "dma_segment", 14: "dma_size", 15: "dma_start",
    16: "dma_end", 19: "private_size", 20: "private_start",
    21: "private_end", 24: "vidpn_source", 25: "flip_interval",
    26: "private_stage", 39: "route", 40: "present_guard", 41: "status",
}
PAIRS = {
    "args": (3, 4),
    "context": (9, 10),
    "dma_physical": (12, 13),
    "private_pointer": (17, 18),
    "dma_virtual": (22, 23),
    "command_context": (27, 28),
    "source_location": (29, 30),
    "destination_location": (31, 32),
}


def decode(path: Path) -> dict:
    fields = {}
    last_field = 0
    accepted = []
    for line in path.read_text(errors="replace").splitlines():
        match = LINE.search(line)
        if not match:
            continue
        word = int(match.group(1), 0)
        command, state, result = map(int, match.groups()[1:])
        if word >> 48 != TAG or command != 0 or state != 3 or result != 0:
            continue
        field = (word >> 32) & 0xFFFF
        value = word & 0xFFFFFFFF
        if field <= last_field:
            raise ValueError(f"non-monotonic or duplicate field {field} after {last_field}")
        fields[field] = value
        accepted.append(word)
        last_field = field
    if not accepted:
        raise ValueError("no accepted EXP507 submit trace words")
    output = {"word_count": len(accepted)}
    for field, name in NAMES.items():
        if field in fields:
            output[name] = fields[field]
    for name, (low, high) in PAIRS.items():
        if low in fields and high in fields:
            output[name] = fields[low] | (fields[high] << 32)
    output["fields"] = {str(field): value for field, value in fields.items()}
    return output


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("host_log", type=Path)
    args = parser.parse_args()
    print(json.dumps(decode(args.host_log), indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
