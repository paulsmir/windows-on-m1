#!/usr/bin/env python3
"""Walk PsLoadedModuleList over kd and report loaded drivers (is pci.sys / stornvme up?)."""
import serial, struct, sys
from kd_proclist import KD, BAUD, require_vuart

PSLOADEDMODULELIST_RVA = 0xD6A7E0
OFF_DLLBASE = 48
OFF_BASEDLLNAME = 88   # _UNICODE_STRING: Length@0 (u16), Buffer@8 (ptr)

ser = serial.Serial(require_vuart(), BAUD, timeout=0.06)
kd = KD(ser)
try:
    print("[*] break in..."); kd.break_in()
    kernbase, _ = kd.get_version()
    print("[*] KernBase 0x%x" % kernbase)
    head = kernbase + PSLOADEDMODULELIST_RVA
    cur = struct.unpack("<Q", kd.read_virtual(head, 8))[0]   # InLoadOrderLinks @0 -> entry = Flink
    names, seen = [], 0
    found_pci = found_storn = False
    while cur != head and seen < 400:
        blk = kd.read_virtual(cur, 0x68)
        nlen = struct.unpack_from("<H", blk, OFF_BASEDLLNAME)[0]
        nbuf = struct.unpack_from("<Q", blk, OFF_BASEDLLNAME + 8)[0]
        name = ""
        if nbuf and 0 < nlen <= 520:
            name = kd.read_virtual(nbuf, nlen).decode("utf-16-le", "replace")
        nl = name.lower()
        names.append(nl)
        if nl.startswith(("pci", "acpi", "storn", "storport", "stornvme", "disk", "partmgr", "volmgr", "msft")):
            print("    [%3d] %s" % (seen, name), flush=True)
        if nl == "pci.sys":
            found_pci = True
        if "stornvme" in nl:
            found_storn = True
        if found_pci and found_storn:
            break               # got what we need; keep the break short
        cur = struct.unpack_from("<Q", blk, 0)[0]
        seen += 1
    print("\n[*] scanned %d modules" % seen)
    print(">>> pci.sys      : %s" % ("YES" if found_pci or "pci.sys" in names else "NO"))
    print(">>> stornvme.sys : %s" % ("YES" if found_storn else "NO"))
    print(">>> acpi.sys     : %s" % ("YES" if "acpi.sys" in names else "NO"))
finally:
    print("[*] go")
    try:
        kd.continue_execution()
    except Exception:
        pass
    ser.close()
