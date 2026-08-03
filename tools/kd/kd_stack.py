#!/usr/bin/env python3
"""Unwind the kernel stack of a target process's main thread over kd and resolve symbols
from ntkrnlmp.pdb. Shows State/WaitReason + the fp-chain so we can tell what it's doing."""
import serial, struct, sys, os, bisect, time
from kd_proclist import (KD, PS_ACTIVE_PROCESS_HEAD_RVA, OFF_ACTIVE_PROCESS_LINKS,
                         OFF_UNIQUE_PROCESS_ID, OFF_IMAGE_FILE_NAME, IMAGE_NAME_LEN, BAUD,
                         require_vuart)

TARGET_NAME = sys.argv[1] if len(sys.argv) > 1 else "SetupHost.exe"

# --- KTHREAD / ETHREAD / EPROCESS offsets from this build's ntkrnlmp.pdb ---
OFF_EP_THREADLISTHEAD = 864     # EPROCESS.ThreadListHead
OFF_ET_THREADLISTENTRY = 1368   # ETHREAD.ThreadListEntry (links EPROCESS.ThreadListHead)
OFF_KT_STACKLIMIT = 48
OFF_KT_STACKBASE = 56
OFF_KT_KERNELSTACK = 88         # -> _KSWITCH_FRAME on the kernel stack
OFF_KT_STATE = 380
OFF_KT_WAITREASON = 675
OFF_ET_WIN32START = 1344
OFF_ET_CID_UNIQUETHREAD = 1256 + 8
# _KSWITCH_FRAME
OFF_SF_FP = 16
OFF_SF_RETURN = 24

STATE = {0: "Initialized", 1: "Ready", 2: "Running", 3: "Standby",
         4: "Terminated", 5: "Waiting", 6: "Transition", 7: "DeferredReady"}
WAIT = {0: "Executive", 4: "DelayExecution", 5: "Suspended", 6: "UserRequest",
        7: "WrExecutive", 13: "WrUserRequest", 15: "WrQueue", 22: "WrCalloutStack",
        27: "WrKernel", 31: "WrPreempted", 33: "WrRundown", 35: "WrDispatchInt"}

SCRATCH = "/private/tmp/claude-502/-Users-pavel-windows/25a1c550-ac63-4e21-98c2-242a046bd455/scratchpad"


def load_syms():
    rvas, names = [], []
    for line in open(os.path.join(SCRATCH, "syms.txt")):
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        try:
            rvas.append(int(parts[0], 16))
        except ValueError:
            continue
        names.append(parts[1].rstrip())
    order = sorted(range(len(rvas)), key=lambda i: rvas[i])
    return [rvas[i] for i in order], [names[i] for i in order]


def sym_for(rva, srvas, snames):
    i = bisect.bisect_right(srvas, rva) - 1
    if i < 0:
        return "?"
    return "%s+0x%x" % (snames[i], rva - srvas[i])


def u64(b, o):
    return struct.unpack_from("<Q", b, o)[0]


def main():
    srvas, snames = load_syms()
    ser = serial.Serial(require_vuart(), BAUD, timeout=0.3)
    kd = KD(ser)
    try:
        print("[*] breaking in..."); kd.break_in()
        kern_base, _ = kd.get_version()
        print("[*] KernBase = 0x%x" % kern_base)

        # find target EPROCESS by name (fresh walk)
        head_va = kern_base + PS_ACTIVE_PROCESS_HEAD_RVA
        cur = u64(kd.read_virtual(head_va, 8), 0)
        eproc = None
        for _ in range(500):
            if cur == head_va:
                break
            ep = cur - OFF_ACTIVE_PROCESS_LINKS
            blk = kd.read_virtual(ep + OFF_UNIQUE_PROCESS_ID, 0x180)
            pid = u64(blk, 0)
            name = blk[OFF_IMAGE_FILE_NAME - OFF_UNIQUE_PROCESS_ID:
                       OFF_IMAGE_FILE_NAME - OFF_UNIQUE_PROCESS_ID + IMAGE_NAME_LEN]
            name = name.split(b"\x00", 1)[0].decode("latin1", "replace")
            if name.lower() == TARGET_NAME.lower():
                eproc = ep
                print("[*] %s  PID %d  EPROCESS 0x%x" % (name, pid, ep))
                break
            cur = u64(blk, OFF_ACTIVE_PROCESS_LINKS - OFF_UNIQUE_PROCESS_ID)
        if eproc is None:
            print("target %r not found" % TARGET_NAME); return

        # main thread = first entry of EPROCESS.ThreadListHead
        tflink = u64(kd.read_virtual(eproc + OFF_EP_THREADLISTHEAD, 8), 0)
        ethread = tflink - OFF_ET_THREADLISTENTRY
        kthread = ethread  # KTHREAD (Tcb) is at ETHREAD+0

        kt = kd.read_virtual(kthread, 0x2C0)   # covers StackLimit..State..WaitReason
        stack_limit = u64(kt, OFF_KT_STACKLIMIT)
        stack_base = u64(kt, OFF_KT_STACKBASE)
        kernel_stack = u64(kt, OFF_KT_KERNELSTACK)
        state = kt[OFF_KT_STATE]
        wait_reason = kt[OFF_KT_WAITREASON]
        tid = u64(kd.read_virtual(ethread + OFF_ET_CID_UNIQUETHREAD, 8), 0)
        win32start = u64(kd.read_virtual(ethread + OFF_ET_WIN32START, 8), 0)

        print("[*] main thread: ETHREAD 0x%x  TID %d" % (ethread, tid))
        print("    State=%s  WaitReason=%s" % (STATE.get(state, str(state)),
                                               WAIT.get(wait_reason, str(wait_reason))))
        print("    StackBase=0x%x  StackLimit=0x%x  KernelStack=0x%x"
              % (stack_base, stack_limit, kernel_stack))
        print("    Win32StartAddress=0x%x (%s)"
              % (win32start, sym_for(win32start - kern_base, srvas, snames)
                 if win32start >= kern_base else "user-mode"))

        # sanity
        if not (stack_limit < kernel_stack <= stack_base and stack_base > 0xffff000000000000):
            print("!! KTHREAD looks off (offsets?): base/limit/ks above"); return

        # read the live kernel stack region [kernel_stack, stack_base) in one buffer
        size = stack_base - kernel_stack
        buf = b""
        a = kernel_stack
        while a < stack_base:
            n = min(0x800, stack_base - a)
            buf += kd.read_virtual(a, n)
            a += n

        def rd(addr):
            o = addr - kernel_stack
            if o < 0 or o + 8 > len(buf):
                return None
            return u64(buf, o)

        # diagnostic: inspect both switch-frame candidates
        print("\n[diag] KernelStack[0:32]: %s" % buf[0:32].hex())
        print("[diag]   u64: %s" % [hex(u64(buf, i)) for i in range(0, 32, 8)])
        sf528 = kt[528:560]
        print("[diag] SwitchFrame@528:  %s" % sf528.hex())
        print("[diag]   u64: %s" % [hex(u64(sf528, i)) for i in range(0, 32, 8)])

        def looks_stack(x):
            return x is not None and stack_limit <= x < stack_base

        # The real switch frame is the one whose saved Fp points back into this kernel stack.
        # In this build that is the embedded KTHREAD.SwitchFrame@528, not [KernelStack].
        fp = rd(kernel_stack + OFF_SF_FP)
        pc = rd(kernel_stack + OFF_SF_RETURN)
        if not looks_stack(fp):
            fp2 = u64(sf528, OFF_SF_FP)
            pc2 = u64(sf528, OFF_SF_RETURN)
            if looks_stack(fp2):
                print("[*] using embedded SwitchFrame@528 (Return=0x%x, Fp=0x%x)" % (pc2, fp2))
                fp, pc = fp2, pc2
        print("\n[*] kernel stack (fp-chain) of %s main thread:\n" % TARGET_NAME)
        depth = 0
        while pc and depth < 64:
            tag = sym_for(pc - kern_base, srvas, snames) if pc >= kern_base else "0x%x (user)" % pc
            print("    #%02d  0x%016x  %s" % (depth, pc, tag))
            depth += 1
            if fp is None or not (stack_limit <= fp < stack_base):
                break
            nxt = rd(fp)          # saved caller fp
            ret = rd(fp + 8)      # saved lr / return address
            if not ret:
                break
            if nxt is not None and nxt <= fp:
                # keep going one more using ret but stop chain
                pc, fp = ret, None
                continue
            pc, fp = ret, nxt
    finally:
        print("\n[*] go (resume)")
        try:
            kd.continue_execution()
        except Exception:
            pass
        ser.close()


if __name__ == "__main__":
    main()
