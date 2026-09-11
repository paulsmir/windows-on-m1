#!/usr/bin/env python3
"""Decode the first EXP514 BuildPagingBuffer broker trace."""
import argparse
import json
import re
from pathlib import Path

TAG = 0x5140
LINE = re.compile(r"AGX power receipt seq=(0x[0-9a-fA-F]+|[0-9]+) cmd=([0-9]+) state=([0-9]+) result=([0-9]+)")
NAMES = {1:"version",2:"irql",3:"operation",4:"dma_size",5:"private_size",6:"status"}

def decode(path: Path) -> dict:
    output = {}
    last = 0
    for line in path.read_text(errors="replace").splitlines():
        match = LINE.search(line)
        if not match:
            continue
        word = int(match.group(1), 0)
        command, state, result = map(int, match.groups()[1:])
        if word >> 48 != TAG or command != 0 or state != 3 or result != 0:
            continue
        field = (word >> 32) & 0xffff
        if field <= last or field not in NAMES:
            raise ValueError(f"invalid paging field {field} after {last}")
        output[NAMES[field]] = word & 0xffffffff
        last = field
    if set(output) != set(NAMES.values()):
        raise ValueError(f"incomplete paging trace: {sorted(set(NAMES.values()) - set(output))}")
    return output

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("host_log", type=Path)
    args = parser.parse_args()
    print(json.dumps(decode(args.host_log), indent=2, sort_keys=True))

if __name__ == "__main__":
    main()
