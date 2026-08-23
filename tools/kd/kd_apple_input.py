#!/usr/bin/env python3
"""Bounded KD summary for the APPL0001 devnode and AppleInput counters."""

import argparse
import serial
import struct

from kd_proclist import BAUD, KD, require_vuart
from kd_watchdog import deadline

IOP_ROOT_DEVICE_NODE_RVA = 0xDCFF40
OFF_SIBLING = 0
OFF_CHILD = 8
OFF_INSTANCEPATH = 40
OFF_SERVICENAME = 56
OFF_STATE = 300
OFF_PROBLEM = 404
STATE = {0x300: "Unspecified", 0x301: "Initialized", 0x302: "DriversAdded",
         0x303: "ResourcesAssigned", 0x304: "StartPending", 0x305: "StartCompletion",
         0x306: "StartPostWork", 0x307: "Started", 0x308: "QueryStopped",
         0x309: "Stopped", 0x30A: "RemovePendingCloses", 0x30B: "Removed"}
PROBLEM = {0: "-", 10: "FAILED_START", 22: "DISABLED", 24: "DEVICE_NOT_THERE",
           28: "FAILED_INSTALL", 31: "FAILED_ADD", 36: "IRQ_TRANSLATION_FAILED"}


def ustr(kd, address):
    block = kd.read_virtual(address, 16)
    length = struct.unpack_from("<H", block, 0)[0]
    buffer = struct.unpack_from("<Q", block, 8)[0]
    if not buffer or not 0 < length <= 512:
        return ""
    return kd.read_virtual(buffer, length).decode("utf-16-le", "replace")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot-va", type=lambda value: int(value, 0))
    args = parser.parse_args()
    ser = serial.Serial(require_vuart(), BAUD, timeout=0.06)
    kd = KD(ser)
    guard = None
    try:
        kd.break_in()
        guard = deadline(15.0)
        guard.__enter__()
        kernel, _ = kd.get_version()
        root = struct.unpack("<Q", kd.read_virtual(
            kernel + IOP_ROOT_DEVICE_NODE_RVA, 8))[0]
        stack = [root]
        found = False
        for _ in range(400):
            if not stack:
                break
            node = stack.pop()
            if not node:
                continue
            block = kd.read_virtual(node, 0x1A0)
            stack.append(struct.unpack_from("<Q", block, OFF_SIBLING)[0])
            stack.append(struct.unpack_from("<Q", block, OFF_CHILD)[0])
            path = ustr(kd, node + OFF_INSTANCEPATH)
            if "APPL0001" not in path.upper():
                continue
            state = struct.unpack_from("<I", block, OFF_STATE)[0]
            problem = struct.unpack_from("<I", block, OFF_PROBLEM)[0]
            service = ustr(kd, node + OFF_SERVICENAME)
            print(f"{path} state={STATE.get(state, hex(state))} "
                  f"problem={problem}({PROBLEM.get(problem, '?')}) "
                  f"service={service}")
            found = True
            break
        if not found:
            print("APPL0001 devnode not found")
        if args.snapshot_va:
            raw = kd.read_virtual(args.snapshot_va, 112)
            version, size, phase = struct.unpack_from("<III", raw, 0)
            counters = struct.unpack_from("<12Q", raw, 16)
            print(f"snapshot v{version} size={size} phase={phase}")
            print("irq=%d queued=%d completed=%d spi=%d timeout=%d "
                  "packet_crc=%d message_crc=%d fragment=%d "
                  "kbd=%d trackpad=%d reset=%d offline=%d" % counters)
    finally:
        if guard is not None:
            guard.__exit__(None, None, None)
        try:
            kd.continue_execution()
        except Exception:
            pass
        ser.close()


if __name__ == "__main__":
    main()
