#!/usr/bin/env python3
"""List the stack frames a Windows PE image reserves through __chkstk.

EXP1230: dwm.exe overflowed a worker thread's stack inside the AGX user-mode
driver (0xC00000FD at __chkstk) because several DDI paths kept 32-89 KB of
state on the stack. Threads that call a UMD (DWM's, a game's) may have small
stacks, so a driver image must not reserve large frames. This scanner needs
no symbols: compilers emit a fixed prologue for frames past one page --

  ARM64:  movz x15, #n[, lsl #16]   ... bl __chkstk ...   sub sp, sp, x15, uxtx #4
          (frame = n * 16)
  x86:    mov eax, #n                call __chkstk / _alloca_probe
          (frame = n; the call target that most such movs precede)

Usage: pe_stack_frames.py IMAGE [--limit BYTES]; exit 1 when a frame exceeds
the limit (default 16 KiB). The pdata function start is printed for ARM64.
"""
from __future__ import annotations

import argparse
import collections
import struct
import sys
from pathlib import Path

MACHINE_ARM64 = 0xAA64
MACHINE_I386 = 0x14C
SUB_SP_X15_UXTX4 = 0xCB2F73FF


def sections(data: bytes):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    machine, count = struct.unpack_from("<HH", data, pe + 4)
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    table = optional + optional_size
    result = []
    for index in range(count):
        entry = table + 40 * index
        virtual_size, rva, raw_size, raw = struct.unpack_from("<IIII", data, entry + 8)
        flags = struct.unpack_from("<I", data, entry + 36)[0]
        result.append((rva, virtual_size, raw, raw_size, flags))
    return machine, optional, result


def scan_arm64(code: bytes, base_rva: int):
    """(rva of the movz, frame bytes) for each ARM64 __chkstk prologue."""
    frames = []
    words = len(code) // 4
    for index in range(words):
        word = struct.unpack_from("<I", code, 4 * index)[0]
        if word & 0xFF80001F != 0xD280000F:      # movz x15, #imm16, lsl #(0|16|32|48)
            continue
        shift = ((word >> 21) & 3) * 16
        value = ((word >> 5) & 0xFFFF) << shift
        for ahead in range(1, 5):
            if index + ahead >= words:
                break
            if struct.unpack_from("<I", code, 4 * (index + ahead))[0] == SUB_SP_X15_UXTX4:
                frames.append((base_rva + 4 * index, value * 16))
                break
    return frames


def scan_x86(code: bytes, base_rva: int):
    """(rva of the mov, frame bytes) for x86 'mov eax, n; call __chkstk'."""
    candidates = []
    for offset in range(len(code) - 10):
        if code[offset] == 0xB8 and code[offset + 5] == 0xE8:
            value = struct.unpack_from("<I", code, offset + 1)[0]
            relative = struct.unpack_from("<i", code, offset + 6)[0]
            if 4096 <= value < (1 << 24):
                target = base_rva + offset + 10 + relative
                candidates.append((base_rva + offset, value, target))
    if not candidates:
        return []
    chkstk = collections.Counter(target for _, _, target in candidates).most_common(1)[0][0]
    return [(rva, value) for rva, value, target in candidates if target == chkstk]


def function_starts(data: bytes, optional: int, layout):
    """ARM64 .pdata function begin RVAs, sorted."""
    exception_rva, exception_size = struct.unpack_from("<II", data, optional + 112 + 3 * 8)
    starts = []
    for rva, virtual_size, raw, raw_size, _ in layout:
        if rva <= exception_rva < rva + max(virtual_size, raw_size):
            offset = raw + exception_rva - rva
            for entry in range(exception_size // 8):
                starts.append(struct.unpack_from("<I", data, offset + 8 * entry)[0])
    return sorted(starts)


def frames(path: Path):
    data = path.read_bytes()
    machine, optional, layout = sections(data)
    found = []
    for rva, virtual_size, raw, raw_size, flags in layout:
        if not flags & 0x20000000:               # IMAGE_SCN_MEM_EXECUTE
            continue
        code = data[raw:raw + min(virtual_size, raw_size)]
        if machine == MACHINE_ARM64:
            found += scan_arm64(code, rva)
        elif machine == MACHINE_I386:
            found += scan_x86(code, rva)
        else:
            raise SystemExit(f"unsupported machine 0x{machine:x}")
    if machine == MACHINE_ARM64:
        import bisect
        starts = function_starts(data, optional, layout)
        found = [(starts[bisect.bisect_right(starts, rva) - 1] if starts and starts[0] <= rva else rva,
                  size) for rva, size in found]
    return machine, sorted(found, key=lambda item: -item[1])


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--limit", type=int, default=16384)
    args = parser.parse_args(argv)
    machine, found = frames(args.image)
    over = [(rva, size) for rva, size in found if size > args.limit]
    for rva, size in found[:12]:
        print(f"0x{rva:08x} {size}")
    print(f"STACK_FRAMES {'FAIL' if over else 'PASS'} machine=0x{machine:x} "
          f"chkstk_frames={len(found)} largest={found[0][1] if found else 0} limit={args.limit}")
    return 1 if over else 0


if __name__ == "__main__":
    sys.exit(main())
