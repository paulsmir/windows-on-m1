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
    21: "context_low",
    22: "context_high",
    23: "command_hash_low",
    24: "command_hash_high",
    25: "dma_bytes_produced",
    26: "patches_produced",
    27: "prepatched",
}
REQUIRED = {NAMES[field] for field in list(range(1, 11)) + [19, 20]}


def _finish(output: dict, ordinal: int) -> dict:
    missing = sorted(REQUIRED - set(output))
    if missing:
        raise ValueError(f"incomplete UMD Render trace: {missing}")
    raw_version = output["version"]
    if raw_version >= 0x10000:
        output["version"] = raw_version >> 16
        output["call_sequence"] = raw_version & 0xFFFF
    else:
        output["call_sequence"] = ordinal
    if "context_low" in output and "context_high" in output:
        output["context_token"] = (
            output.pop("context_low") |
            (output.pop("context_high") << 32)
        )
    if "command_hash_low" in output and "command_hash_high" in output:
        output["command_hash"] = (
            output.pop("command_hash_low") |
            (output.pop("command_hash_high") << 32)
        )
    return output


def decode_calls(path: Path) -> list[dict]:
    calls = []
    output = None
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
        if field not in NAMES:
            raise ValueError(f"unknown UMD Render field {field}")
        if field == 1:
            if output is not None:
                calls.append(_finish(output, len(calls) + 1))
            output = {}
        if output is None:
            continue
        output[NAMES[field]] = value
    if output is not None:
        calls.append(_finish(output, len(calls) + 1))
    if not calls:
        raise ValueError("incomplete UMD Render trace: no calls")
    return calls


def decode(path: Path) -> dict:
    return decode_calls(path)[-1]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("host_log", type=Path)
    args = parser.parse_args()
    print(json.dumps({"calls": decode_calls(args.host_log)},
                     indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
