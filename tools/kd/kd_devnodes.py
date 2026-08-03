#!/usr/bin/env python3
"""Walk the PnP devnode tree over kd from IopRootDeviceNode and report each node's
InstancePath / State / Problem / Service - i.e. whether our ACPI PNP0A08 (PCI0) root was
created, started, or failed, and with which CM_PROB_* code."""
import serial, struct, sys
from kd_proclist import KD, BAUD, require_vuart
from kd_watchdog import deadline

IOP_ROOT_DEVICE_NODE_RVA = 0xDCFF40
# _DEVICE_NODE offsets (this build's ntkrnlmp.pdb)
OFF_SIBLING = 0
OFF_CHILD = 8
OFF_INSTANCEPATH = 40      # UNICODE_STRING: Length@0(u16), Buffer@8
OFF_SERVICENAME = 56       # UNICODE_STRING
OFF_STATE = 300
OFF_FLAGS = 396
OFF_PROBLEM = 404

STATE = {0x300: "Unspecified", 0x301: "Initialized", 0x302: "DriversAdded",
         0x303: "ResourcesAssigned", 0x304: "StartPending", 0x305: "StartCompletion",
         0x306: "StartPostWork", 0x307: "Started", 0x308: "QueryStopped",
         0x309: "Stopped", 0x30a: "RemovePendingCloses", 0x30b: "Removed",
         0x30c: "DeletePendingCloses", 0x30d: "Deleted"}
PROBLEM = {0: "-", 1: "NOT_CONFIGURED", 2: "DEVLOADER_FAILED", 3: "OUT_OF_MEMORY",
           10: "FAILED_START", 12: "NORMAL_CONFLICT", 13: "NOT_VERIFIED",
           14: "NEED_RESTART", 18: "REINSTALL", 19: "REGISTRY", 22: "DISABLED",
           24: "DEVICE_NOT_THERE", 27: "INVALID_LOG_CONF", 28: "FAILED_INSTALL",
           29: "HARDWARE_DISABLED", 31: "FAILED_ADD", 35: "BIOS_TABLE",
           36: "IRQ_TRANSLATION_FAILED", 43: "FAILED_POST_START",
           51: "WAITING_ON_DEPENDENCY"}

ser = serial.Serial(require_vuart(), BAUD, timeout=0.06)
kd = KD(ser)


def ustr(kd, addr):
    blk = kd.read_virtual(addr, 16)
    ln = struct.unpack_from("<H", blk, 0)[0]
    buf = struct.unpack_from("<Q", blk, 8)[0]
    if not buf or not (0 < ln <= 512):
        return ""
    return kd.read_virtual(buf, ln).decode("utf-16-le", "replace")


watchdog = None
try:
    print("[*] break in..."); kd.break_in()
    # A full PnP walk over 115200-baud KD can take minutes when one memory read
    # needs retries. Never leave the guest stopped for that long: the watchdog
    # unwinds into the finally block below, which always sends Continue.
    watchdog = deadline(15.0)
    watchdog.__enter__()
    kernbase, _ = kd.get_version()
    root = struct.unpack("<Q", kd.read_virtual(kernbase + IOP_ROOT_DEVICE_NODE_RVA, 8))[0]
    print("[*] IopRootDeviceNode = 0x%x" % root)

    stack = [(root, 0)]
    n = 0
    hits = []
    while stack and n < 400:
        node, depth = stack.pop()
        if not node:
            continue
        blk = kd.read_virtual(node, 0x1A0)
        sib = struct.unpack_from("<Q", blk, OFF_SIBLING)[0]
        child = struct.unpack_from("<Q", blk, OFF_CHILD)[0]
        state = struct.unpack_from("<I", blk, OFF_STATE)[0]
        flags = struct.unpack_from("<I", blk, OFF_FLAGS)[0]
        prob = struct.unpack_from("<I", blk, OFF_PROBLEM)[0]
        path = ustr(kd, node + OFF_INSTANCEPATH)
        svc = ustr(kd, node + OFF_SERVICENAME)
        n += 1
        line = "%s%-46s st=%-16s prob=%-2d(%s) svc=%s" % (
            "  " * min(depth, 6), path or "(root)", STATE.get(state, hex(state)),
            prob, PROBLEM.get(prob, "?"), svc)
        up = path.upper()
        interesting = ("PNP0A08" in up or "PNP0A03" in up or "PCI" in up
                       or "PNP0D10" in up or "APPL8103" in up or "USB" in up or "HID" in up
                       or prob != 0 or "PNP0C02" in up)
        if interesting:
            print(line, flush=True)
            hits.append((path, state, prob, svc))
        stack.append((sib, depth))
        stack.append((child, depth + 1))

    print("\n[*] scanned %d devnodes, %d interesting" % (n, len(hits)))
    pci_root = [h for h in hits if "PNP0A08" in h[0].upper() or "PNP0A03" in h[0].upper()]
    if pci_root:
        for p, st, pr, sv in pci_root:
            print(">>> PCI ROOT: %s state=%s problem=%d(%s) service=%r"
                  % (p, STATE.get(st, hex(st)), pr, PROBLEM.get(pr, "?"), sv))
    else:
        print(">>> NO PNP0A08/PNP0A03 devnode - Windows never created the PCI root from DSDT")
except TimeoutError as exc:
    print("[!] %s; returning control to the guest" % exc, flush=True)
finally:
    if watchdog is not None:
        watchdog.__exit__(None, None, None)
    print("[*] go")
    try:
        kd.continue_execution()
    except Exception:
        pass
    ser.close()
