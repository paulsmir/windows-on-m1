#!/usr/bin/env python3
"""Break in and read a physical address over kd (to probe whether m1n1's ECAM hook traps)."""
import serial, sys, struct
from kd_proclist import KD, BAUD, require_vuart

ADDR = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x690000000
LEN = int(sys.argv[2], 0) if len(sys.argv) > 2 else 16

ser = serial.Serial(require_vuart(), BAUD, timeout=0.06)
kd = KD(ser)
try:
    print("[*] break in..."); kd.break_in()
    kd.get_version()
    ret, mem = kd.read_physical(ADDR, LEN)
    print("[*] phys 0x%x [%d]: status=0x%x" % (ADDR, LEN, ret))
    print("    bytes: %s" % mem.hex())
    if len(mem) >= 4:
        vid = struct.unpack_from("<H", mem, 0)[0]
        did = struct.unpack_from("<H", mem, 2)[0]
        print("    as PCI cfg[0]: VID=0x%04x DID=0x%04x  (want 1b36:0010)" % (vid, did))
finally:
    print("[*] go")
    try:
        kd.continue_execution()
    except Exception:
        pass
    ser.close()
