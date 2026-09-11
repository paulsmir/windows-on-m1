#!/usr/bin/env python3
"""Decode the first EXP513 OpenAllocation broker trace."""

import argparse
import json
import re
from pathlib import Path


TAG = 0x5130
LINE = re.compile(
    r"AGX power receipt seq=(0x[0-9a-fA-F]+|[0-9]+) "
    r"cmd=([0-9]+) state=([0-9]+) result=([0-9]+)"
)
NAMES = {
    1: "version",
    2: "irql",
    3: "device_flags",
    4: "allocation_count",
    5: "open_flags",
    6: "subresource",
    7: "allocation_handle",
    8: "private_size",
    9: "device_specific_present",
    10: "guard",
    11: "status",
}


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
                f"non-monotonic OpenAllocation field {field} after {last_field}"
            )
        if field not in NAMES:
            raise ValueError(f"unknown OpenAllocation field {field}")
        output[NAMES[field]] = value
        last_field = field
    if set(output) != set(NAMES.values()):
        missing = sorted(set(NAMES.values()) - set(output))
        raise ValueError(f"incomplete OpenAllocation trace: {missing}")
    return output


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("host_log", type=Path)
    args = parser.parse_args()
    print(json.dumps(decode(args.host_log), indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
