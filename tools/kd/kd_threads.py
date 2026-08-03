#!/usr/bin/env python3
"""Enumerate every thread of a target process over kd: State, WaitReason, top kernel frames
(what it's blocked in), and Win32StartAddress. Answers 'who waits for input / do all wait'."""
import serial, sys
from kd_proclist import (KD, PS_ACTIVE_PROCESS_HEAD_RVA, OFF_ACTIVE_PROCESS_LINKS,
                         OFF_UNIQUE_PROCESS_ID, OFF_IMAGE_FILE_NAME, IMAGE_NAME_LEN, BAUD,
                         require_vuart)
from kd_stack import (load_syms, sym_for, u64, STATE, WAIT,
                      OFF_EP_THREADLISTHEAD, OFF_ET_THREADLISTENTRY, OFF_KT_STACKLIMIT,
                      OFF_KT_STACKBASE, OFF_KT_KERNELSTACK, OFF_KT_STATE, OFF_KT_WAITREASON,
                      OFF_ET_WIN32START, OFF_ET_CID_UNIQUETHREAD, OFF_SF_FP, OFF_SF_RETURN)

TARGET = sys.argv[1] if len(sys.argv) > 1 else "SetupHost.exe"


def top_frames(kd, kthread, kern_base, srvas, snames, maxf=9):
    kt = kd.read_virtual(kthread, 0x2C0)
    slim = u64(kt, OFF_KT_STACKLIMIT); sbase = u64(kt, OFF_KT_STACKBASE)
    kstk = u64(kt, OFF_KT_KERNELSTACK); state = kt[OFF_KT_STATE]; wr = kt[OFF_KT_WAITREASON]
    sf = kt[528:560]
    # pick the switch frame whose saved Fp lands in the stack (embedded @528 in this build)
    fp = u64(sf, OFF_SF_FP); pc = u64(sf, OFF_SF_RETURN)
    frames = []
    if slim <= fp < sbase and sbase > 0xffff000000000000:
        hi = min(sbase, kstk + 0x2000)
        buf = b""; a = kstk
        while a < hi:
            n = min(0x800, hi - a); buf += kd.read_virtual(a, n); a += n

        def rd(x):
            o = x - kstk
            return u64(buf, o) if 0 <= o + 8 <= len(buf) else None
        d = 0
        while pc and d < maxf:
            frames.append(pc); d += 1
            if not (slim <= fp < sbase):
                break
            nxt = rd(fp); ret = rd(fp + 8)
            if not ret or nxt is None or nxt <= fp:
                break
            pc, fp = ret, nxt
    syms = [sym_for(f - kern_base, srvas, snames) if f >= kern_base else "user:0x%x" % f
            for f in frames]
    return state, wr, syms


def classify(syms, wr):
    j = " ".join(syms)
    if "NtWaitForWorkViaWorkerFactory" in j:
        return "threadpool idle (no work)"
    if "NtRemoveIoCompletion" in j:
        return "I/O completion wait"
    if "NtDelayExecution" in j:
        return "sleep/delay"
    if "NtUserGetMessage" in j or "NtUserMsgWait" in j:
        return ">>> MESSAGE/INPUT wait"
    if "NtWaitForMultipleObjects" in j or "NtWaitForSingleObject" in j:
        return (">>> wait on objects (events/INPUT)" if wr in (6, 13)
                else "wait on objects")
    if "KiSystemServiceCopyEnd" in j and wr in (6, 13):
        return ">>> syscall wait, UserRequest (win32k/input?)"
    return "wait (%s)" % (WAIT.get(wr, wr))


def main():
    srvas, snames = load_syms()
    ser = serial.Serial(require_vuart(), BAUD, timeout=0.06)
    kd = KD(ser)
    try:
        print("[*] breaking in..."); kd.break_in()
        kern_base, _ = kd.get_version()
        head_va = kern_base + PS_ACTIVE_PROCESS_HEAD_RVA
        cur = u64(kd.read_virtual(head_va, 8), 0)
        eproc = None
        for _ in range(500):
            if cur == head_va:
                break
            ep = cur - OFF_ACTIVE_PROCESS_LINKS
            blk = kd.read_virtual(ep + OFF_UNIQUE_PROCESS_ID, 0x180)
            nm = blk[OFF_IMAGE_FILE_NAME - OFF_UNIQUE_PROCESS_ID:
                     OFF_IMAGE_FILE_NAME - OFF_UNIQUE_PROCESS_ID + IMAGE_NAME_LEN]
            nm = nm.split(b"\x00", 1)[0].decode("latin1", "replace")
            if nm.lower() == TARGET.lower():
                eproc = ep; print("[*] %s EPROCESS 0x%x" % (nm, ep)); break
            cur = u64(blk, OFF_ACTIVE_PROCESS_LINKS - OFF_UNIQUE_PROCESS_ID)
        if eproc is None:
            print("not found"); return

        thead = eproc + OFF_EP_THREADLISTHEAD
        link = u64(kd.read_virtual(thead, 8), 0)
        n = 0; waiting = 0; input_like = 0
        print("\n%-6s %-9s %-14s %-32s %s" % ("TID", "State", "WaitReason", "class", "top frame"))
        print("-" * 100)
        while link != thead and n < 200:
            et = link - OFF_ET_THREADLISTENTRY
            kth = et  # KTHREAD at ETHREAD+0
            tid = u64(kd.read_virtual(et + OFF_ET_CID_UNIQUETHREAD, 8), 0)
            state, wr, syms = top_frames(kd, kth, kern_base, srvas, snames)
            cls = classify(syms, wr)
            top = syms[0] if syms else "(running/no-frame)"
            # find the Nt* service frame for readability
            svc = next((s for s in syms if s.startswith("Nt") or "NtUser" in s), top)
            print("%-6d %-9s %-14s %-32s %s" % (tid, STATE.get(state, state),
                  WAIT.get(wr, wr), cls, svc))
            n += 1
            if state == 5:
                waiting += 1
            if ">>>" in cls:
                input_like += 1
            link = u64(kd.read_virtual(et + OFF_ET_THREADLISTENTRY, 8), 0)
        print("\n[*] %d threads total: %d Waiting, %d not-waiting; %d input/event-wait candidates"
              % (n, waiting, n - waiting, input_like))
    finally:
        print("[*] go")
        try:
            kd.continue_execution()
        except Exception:
            pass
        ser.close()


if __name__ == "__main__":
    main()
