#!/usr/bin/env python3
"""Decode AGX channel/terminal host trace words without manual bit slicing."""

import argparse
import json
import re
from pathlib import Path


LINE = re.compile(r"AGX power receipt seq=(0x[0-9a-fA-F]+|[0-9]+)")
CHANNEL_TAG = 0x5460
TERMINAL_TAG = 0x54A0
EXIT_TAG = 0x54B0


def decode_word(word):
    tag = (word >> 48) & 0xFFFF
    if tag == CHANNEL_TAG:
        return {
            "kind": "channel_progress",
            "ta_read": (word >> 32) & 0xFFFF,
            "d3_read": (word >> 16) & 0xFFFF,
            "fence": word & 0xFFFF,
        }
    if tag == TERMINAL_TAG:
        return {
            "kind": "terminal_observation",
            "source": (word >> 40) & 0xFF,
            "completion_status": (word >> 32) & 0xFF,
            "runtime_phase": (word >> 24) & 0xFF,
            "provider_phase": (word >> 16) & 0xFF,
            "fence": word & 0xFFFF,
        }
    if tag == EXIT_TAG:
        return {
            "kind": "worker_exit",
            "reason": (word >> 40) & 0xFF,
            "valid_mask": (word >> 32) & 0xFF,
            "runtime_phase": (word >> 24) & 0xFF,
            "provider_phase": (word >> 16) & 0xFF,
            "fence": word & 0xFFFF,
        }
    return None


def decode(path):
    records = []
    for match in LINE.finditer(Path(path).read_text(errors="replace")):
        record = decode_word(int(match.group(1), 0))
        if record is not None:
            records.append(record)
    return records


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("host_log", type=Path)
    args = parser.parse_args()
    print(json.dumps(decode(args.host_log), indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
