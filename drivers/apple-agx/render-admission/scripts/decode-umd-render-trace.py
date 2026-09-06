#!/usr/bin/env python3
"""Decode the first EXP512 UMD DxgkDdiRender broker trace."""

import argparse
import json
import re
from pathlib import Path


TAG = 0x5120
LINE = re.compile(
    r"AGX power receipt seq=(0x[0-9a-fA-F]+|[0-9]+) "
    r"cmd=([0-9]+) state=([0-9]+) result=([0-9]+)"
)
NAMES = {
    1: "version",
    2: "irql",
    3: "context_flags",
    4: "command_length",
    5: "dma_size",
    6: "private_size",
    7: "allocation_count",
    8: "patch_in_count",
    9: "patch_out_count",
    10: "multipass",
    11: "command_magic",
    12: "command_version",
    13: "command_bytes",
    14: "command_opcode",
    15: "destination_index",
    16: "color",
    17: "rop",
    18: "rop3",
    19: "guard",
    20: "status",
}
REQUIRED = {NAMES[field] for field in list(range(1, 11)) + [19, 20]}


def decode(path: Path) -> dict:
    output = {}
    last_field = 0
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
            raise ValueError(
                f"non-monotonic UMD Render field {field} after {last_field}"
            )
        if field not in NAMES:
            raise ValueError(f"unknown UMD Render field {field}")
        output[NAMES[field]] = value
        last_field = field
    missing = sorted(REQUIRED - set(output))
    if missing:
        raise ValueError(f"incomplete UMD Render trace: {missing}")
    return output


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("host_log", type=Path)
    args = parser.parse_args()
    print(json.dumps(decode(args.host_log), indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
