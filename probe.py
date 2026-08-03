#!/usr/bin/env python3
# Connect to m1n1 on the Air over the USB proxy and print machine identity.
# Read-only: only reads the Apple Device Tree and a few proxy fields.
import os, sys, pathlib

device = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("M1N1DEVICE")
if not device:
    raise SystemExit("usage: M1N1DEVICE=/dev/cu.PROXY probe.py [DEVICE]")
os.environ.setdefault("M1N1DEVICE", device)

sys.path.insert(0, str(pathlib.Path(__file__).parent / "m1n1_windows" / "proxyclient"))

from m1n1.setup import *

print("=" * 50)
chosen = u.adt["/chosen"]
for attr in ("machine-name", "model-number", "board-id", "chip-id"):
    try:
        v = getattr(chosen, attr.replace("-", "_"))
        print(f"{attr:15s} = {v}")
    except AttributeError:
        pass

print(f"{'CPUs':15s} = {len(list(u.adt['cpus']))}")
try:
    dram = u.adt["/chosen"].dram_size
    print(f"{'DRAM':15s} = {dram / (1024**3):.1f} GiB")
except AttributeError:
    pass
print(f"{'m1n1 base':15s} = 0x{u.base:x}")
print("=" * 50)
print("m1n1 proxy connection established.")
