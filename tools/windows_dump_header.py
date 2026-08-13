#!/usr/bin/env python3
"""Read the architecture and bugcheck fields from an ARM64 DUMP_HEADER64."""

from dataclasses import dataclass
from pathlib import Path
import argparse
import json
import struct


HEADER_SIZE = 0x60
ARM64_MACHINE = 0xAA64


class DumpFormatError(ValueError):
    pass


@dataclass(frozen=True)
class DumpHeader:
    machine: str
    processor_count: int
    bugcheck_code: int
    parameters: tuple[int, int, int, int]
    hung_cpu: int | None = None
    hung_prcb: int | None = None

    def as_dict(self):
        return {
            "machine": self.machine,
            "processor_count": self.processor_count,
            "bugcheck_code": self.bugcheck_code,
            "parameters": list(self.parameters),
            "hung_cpu": self.hung_cpu,
            "hung_prcb": self.hung_prcb,
        }


def parse_dump_header(path):
    data = Path(path).read_bytes()[:HEADER_SIZE]
    if len(data) < HEADER_SIZE or data[:8] != b"PAGEDU64":
        raise DumpFormatError("not a 64-bit Windows crash dump")

    machine, processor_count, bugcheck = struct.unpack_from("<III", data, 0x30)
    if machine != ARM64_MACHINE:
        raise DumpFormatError(f"unsupported machine type 0x{machine:04x}; expected ARM64")
    parameters = struct.unpack_from("<4Q", data, 0x40)

    hung_prcb = parameters[2] if bugcheck == 0x101 else None
    hung_cpu = parameters[3] if bugcheck == 0x101 else None
    return DumpHeader("arm64", processor_count, bugcheck, parameters,
                      hung_cpu=hung_cpu, hung_prcb=hung_prcb)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", type=Path)
    args = parser.parse_args(argv)
    print(json.dumps(parse_dump_header(args.dump).as_dict(), indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
