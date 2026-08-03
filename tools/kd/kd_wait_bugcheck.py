#!/usr/bin/env python3
"""Listen on the kd channel and decode whatever state change the target reports.

With a debugger attached Windows stops on a bugcheck instead of rebooting, and the
DBGKD_ANY_WAIT_STATE_CHANGE it sends carries the bugcheck code and its four parameters.
For IRQL_NOT_LESS_OR_EQUAL (0xA) those are: referenced address, IRQL at the time, 0=read
1=write, and the address of the instruction that did it - which is exactly what is needed
instead of guessing.

This does NOT break in: it only reads, so the target's own timing is untouched.
"""
import os
import serial, struct, sys, time

PORT = os.environ.get("M1N1VUART")
if not PORT:
    raise SystemExit("set M1N1VUART to the guest virtual-UART device")
BAUD = 115200
DATA_LEADER = 0x30303030
CTRL_LEADER = 0x69696969
PKT_STATE_CHANGE64 = 7
PKT_DEBUG_IO = 3

# DbgKdExceptionStateChange payload: DBGKM_EXCEPTION64 at offset 32 of the state change.
# EXCEPTION_RECORD64: Code(4) Flags(4) Record(8) Address(8) NumberParameters(4) pad(4)
#                     Information[15] (8 each)
STATE_HDR = 32
EXC_CODE = STATE_HDR + 0
EXC_ADDRESS = STATE_HDR + 16
EXC_NPARAMS = STATE_HDR + 24
EXC_PARAMS = STATE_HDR + 32

BUGCHECK_NAMES = {
    0x0A: "IRQL_NOT_LESS_OR_EQUAL",
    0x1E: "KMODE_EXCEPTION_NOT_HANDLED",
    0x50: "PAGE_FAULT_IN_NONPAGED_AREA",
    0x7E: "SYSTEM_THREAD_EXCEPTION_NOT_HANDLED",
    0x101: "CLOCK_WATCHDOG_TIMEOUT",
    0x133: "DPC_WATCHDOG_VIOLATION",
    0x139: "KERNEL_SECURITY_CHECK_FAILURE",
}

timeout_s = float(sys.argv[1]) if len(sys.argv) > 1 else 300.0
ser = serial.Serial(PORT, BAUD, timeout=0.06)

#
# Attach properly first. Passive listening is not enough: Windows only stops on a bugcheck
# when a debugger is actually connected, otherwise it reboots. So do the KDCOM handshake
# (break in, read the version, continue) and only then wait for the fault.
#
from kd_proclist import KD
_kd = KD(ser)
try:
    print("[*] connecting to KD...", flush=True)
    _kd.break_in()
    kb, _ = _kd.get_version()
    print(f"[*] debugger connected, KernBase=0x{kb:x}; resuming guest", flush=True)
    _kd.continue_execution()
except Exception as e:
    print(f"[!] debugger attach failed: {e!r}; listening passively", flush=True)

ser.timeout = 0.3
print(f"[*] waiting up to {timeout_s:.0f}s for a bugcheck", flush=True)

deadline = time.time() + timeout_s
window = b""
seen = 0
try:
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            continue
        window = (window + b)[-4:]
        if len(window) < 4:
            continue
        lead = struct.unpack("<I", window)[0]
        if lead not in (DATA_LEADER, CTRL_LEADER):
            continue
        hdr = ser.read(12)
        if len(hdr) < 12:
            continue
        ptype, count, pid, csum = struct.unpack("<HHII", hdr)
        if lead == CTRL_LEADER:
            continue
        data = b""
        while len(data) < count and time.time() < deadline:
            chunk = ser.read(count - len(data))
            if chunk:
                data += chunk
        ser.read(1)
        if ptype == PKT_DEBUG_IO:
            txt = data[32:].split(b"\x00", 1)[0].decode("latin1", "replace").strip()
            if txt:
                print("  [target] " + txt, flush=True)
            continue
        if ptype != PKT_STATE_CHANGE64 or len(data) < EXC_PARAMS + 32:
            continue
        seen += 1
        new_state = struct.unpack_from("<I", data, 0)[0]
        code = struct.unpack_from("<I", data, EXC_CODE)[0]
        addr = struct.unpack_from("<Q", data, EXC_ADDRESS)[0]
        nparams = struct.unpack_from("<I", data, EXC_NPARAMS)[0]
        nparams = min(nparams, 15)
        params = [struct.unpack_from("<Q", data, EXC_PARAMS + 8 * i)[0] for i in range(nparams)]

        print(f"\n=== STATE CHANGE #{seen}  NewState=0x{new_state:x} ===", flush=True)
        print(f"    ExceptionCode = 0x{code:08x}   at 0x{addr:016x}", flush=True)
        for i, p in enumerate(params):
            print(f"    param[{i}] = 0x{p:016x}", flush=True)

        # A bugcheck arrives as a KERNEL_APC/breakpoint-style exception whose parameters are
        # the bugcheck code followed by its four arguments.
        if params:
            bc = params[0]
            if bc in BUGCHECK_NAMES or bc < 0x200:
                print(f"\n>>> BUGCHECK 0x{bc:x} {BUGCHECK_NAMES.get(bc,'')}", flush=True)
                if bc == 0x0A and len(params) >= 5:
                    print(f"    referenced address : 0x{params[1]:016x}", flush=True)
                    print(f"    IRQL               : {params[2]}", flush=True)
                    print(f"    access             : {'write' if params[3] else 'read'}", flush=True)
                    print(f"    faulting instr     : 0x{params[4]:016x}", flush=True)
                break
finally:
    ser.close()
    print(f"\n[*] finished, state changes received: {seen}")
