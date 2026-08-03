#!/usr/bin/env python3
"""
Minimal Windows KD (KDCOM serial) client that reproduces `!process 0 0`: break into the
live kernel, walk PsActiveProcessHead, print PID + image name for every EPROCESS, then go.

Design notes / the three pitfalls the walk has to respect:
  * ActiveProcessLinks is a LIST_ENTRY *inside* EPROCESS (offset 0x1C8). Each list pointer
    points at that field, so EPROCESS = ptr - 0x1C8 (CONTAINING_RECORD).
  * The list head (PsActiveProcessHead) is NOT a process; the stop condition is returning
    to the head address, not NULL. First node after the head is System (PID 4).
  * ImageFileName is UCHAR[15] with no guaranteed NUL - truncate at the first 0 / to 15.

Offsets are from THIS build's ntkrnlmp.pdb (they float between builds). KernBase comes from
DbgKdGetVersionApi at runtime, so PsActiveProcessHead VA = KernBase + RVA.

Keep the freeze short: break -> reads -> go (go runs in `finally`, even on error), because
the guest's clock is stopped while broken and a long freeze means a post-resume timer-IRQ
avalanche.
"""
import serial, struct, sys, time, os

DEBUG = bool(os.environ.get("KD_DEBUG"))


def dbg(*a):
    if DEBUG:
        print(*a, flush=True)

PORT = os.environ.get("M1N1VUART")
BAUD = 115200


def require_vuart():
    if not PORT:
        raise SystemExit("set M1N1VUART to the guest virtual-UART device")
    return PORT

# --- from ntkrnlmp.pdb (this build) ---
PS_ACTIVE_PROCESS_HEAD_RVA = 0xD7AF80
OFF_UNIQUE_PROCESS_ID = 0x1C0
OFF_ACTIVE_PROCESS_LINKS = 0x1C8
OFF_IMAGE_FILE_NAME = 0x328
IMAGE_NAME_LEN = 15

# --- KDCOM protocol constants ---
DATA_LEADER = 0x30303030
CTRL_LEADER = 0x69696969
TRAIL = 0xAA
BREAKIN = 0x62

PKT_STATE_CHANGE64 = 7
PKT_STATE_MANIPULATE = 2
PKT_DEBUG_IO = 3
PKT_ACK = 4
PKT_RESEND = 5
PKT_RESET = 6

API_READ_VIRTUAL = 0x3130
API_GET_VERSION = 0x3146
API_CONTINUE = 0x313C
API_REBOOT = 0x313B
DBG_CONTINUE = 0x00010002

# The target's initial/resync packets carry the SYNC bit (0x800) in the PacketId; responses
# (ACKs and our requests) must clear it. Base host id is therefore 0x80800000, toggling bit 0.
SYNC_BIT = 0x800
INITIAL_HOST_ID = 0x80800000


def cksum(b):
    return sum(b) & 0xFFFFFFFF


class KD:
    def __init__(self, ser):
        self.ser = ser
        self.host_id = INITIAL_HOST_ID

    # ---- raw io ----
    def _read_exact(self, n, deadline):
        buf = b""
        while len(buf) < n:
            if time.time() > deadline:
                return None
            chunk = self.ser.read(n - len(buf))
            if chunk:
                buf += chunk
        return buf

    def _send_data(self, ptype, data, pid):
        hdr = struct.pack("<IHHII", DATA_LEADER, ptype, len(data), pid, cksum(data))
        self.ser.write(hdr + data + bytes([TRAIL]))

    def _send_ctrl(self, ptype, pid):
        self.ser.write(struct.pack("<IHHII", CTRL_LEADER, ptype, 0, pid, 0))

    def _ack(self, pid):
        # Clear the SYNC bit: the target ignores an ACK that still carries it.
        self._send_ctrl(PKT_ACK, pid & ~SYNC_BIT)

    # ---- packet framing ----
    def recv(self, timeout=6.0):
        """Return (ptype, pid, data) or None on timeout. Resyncs on a leader."""
        deadline = time.time() + timeout
        window = b""
        while True:
            if time.time() > deadline:
                return None
            b = self.ser.read(1)
            if not b:
                continue
            window = (window + b)[-4:]
            lead = struct.unpack("<I", window)[0] if len(window) == 4 else 0
            if lead not in (DATA_LEADER, CTRL_LEADER):
                continue
            rest = self._read_exact(12, deadline)
            if rest is None:
                return None
            ptype, count, pid, csum = struct.unpack("<HHII", rest)
            if lead == CTRL_LEADER:
                return (ptype, pid, b"")
            data = self._read_exact(count, deadline)
            if data is None:
                return None
            self._read_exact(1, deadline)  # trailing 0xAA
            if cksum(data) != csum:
                # keep going; a bad checksum means resync, the target will resend
                dbg("  [recv] BAD CKSUM type=%d pid=0x%x len=%d" % (ptype, pid, len(data)))
                continue
            dbg("  [recv] type=%d pid=0x%x len=%d" % (ptype, pid, len(data)))
            return (ptype, pid, data)

    def break_in(self):
        """Send breakin, absorb noise, return the state-change packet."""
        for _ in range(3):
            self.ser.write(bytes([BREAKIN]))
            time.sleep(0.05)
        deadline = time.time() + 10.0
        while time.time() < deadline:
            pkt = self.recv(timeout=2.0)
            if pkt is None:
                self.ser.write(bytes([BREAKIN]))
                continue
            ptype, pid, data = pkt
            if ptype == PKT_STATE_CHANGE64:
                self._ack(pid)
                return data
            if ptype == PKT_DEBUG_IO:
                self._ack(pid)          # target printf noise, e.g. "Refreshing KD"
            # ignore acks/others while syncing
        raise RuntimeError("no STATE_CHANGE64 after breakin")

    def _drain_and_ack(self, secs):
        """Absorb pending packets for `secs`, acking any state-change / debug-io so the
        target settles into KdpSendWaitContinue (waiting for a manipulate)."""
        deadline = time.time() + secs
        while time.time() < deadline:
            pkt = self.recv(timeout=0.1)
            if pkt is None:
                return          # quiet -> synced, nothing pending
            ptype, pid, _ = pkt
            if ptype in (PKT_STATE_CHANGE64, PKT_DEBUG_IO):
                self._ack(pid)

    # ---- manipulate-state request/reply ----
    def manipulate(self, api, union_body=b""):
        """Send a DBGKD_MANIPULATE_STATE64 request, return (return_status, reply_data).
        A rejected request shows up as the target re-sending its last data packet (the
        state change) with no type-5 on the wire, so we alternate the packet id and
        re-ack before each try until the reply (type 2) comes back."""
        m64 = bytearray(56)                 # sizeof(DBGKD_MANIPULATE_STATE64); union @16
        struct.pack_into("<I", m64, 0, api)
        m64[16:16 + len(union_body)] = union_body
        payload = bytes(m64)

        for attempt in range(10):
            pid = self.host_id ^ (attempt & 1)   # track the toggling id; retry flips it
            self._drain_and_ack(0.15)       # get the target into wait-continue, ack pending
            dbg("  [send] manipulate api=0x%x pid=0x%x attempt=%d" % (api, pid, attempt))
            self._send_data(PKT_STATE_MANIPULATE, payload, pid)

            deadline = time.time() + 1.6
            while time.time() < deadline:
                pkt = self.recv(timeout=1.0)
                if pkt is None:
                    break
                ptype, rpid, data = pkt
                if ptype == PKT_STATE_MANIPULATE:
                    self._ack(rpid)
                    self.host_id = (rpid & ~SYNC_BIT) ^ 1   # next request uses the toggled id
                    ret = struct.unpack_from("<I", data, 8)[0]
                    return ret, data
                if ptype in (PKT_STATE_CHANGE64, PKT_DEBUG_IO):
                    self._ack(rpid)         # request rejected/ignored; keep acking
                # ACK/RESEND: fall through to next attempt (toggled id)
        raise RuntimeError("manipulate(api=0x%x) failed" % api)

    def get_version(self):
        ret, data = self.manipulate(API_GET_VERSION)
        # GetVersion64 union @16: KernBase @ +16 (=packet off 32), PsLoadedModuleList @ +24
        kern_base = struct.unpack_from("<Q", data, 32)[0]
        ps_loaded = struct.unpack_from("<Q", data, 40)[0]
        return kern_base, ps_loaded

    def read_virtual(self, addr, length):
        # DBGKD_READ_MEMORY64 @16: TargetBaseAddress(8), TransferCount(4), ActualBytesRead(4)
        body = struct.pack("<QII", addr, length, 0)
        ret, data = self.manipulate(API_READ_VIRTUAL, body)
        actual = struct.unpack_from("<I", data, 28)[0]  # ActualBytesRead @ union+12
        mem = data[-actual:] if actual else b""
        if ret & 0x80000000 or len(mem) < length:
            raise RuntimeError("read_virtual(0x%x,%d) status=0x%x got=%d"
                               % (addr, length, ret, len(mem)))
        return mem

    def read_physical(self, addr, length):
        # DbgKdReadPhysicalMemoryApi (0x313D): same DBGKD_READ_MEMORY64 union as virtual.
        body = struct.pack("<QII", addr, length, 0)
        ret, data = self.manipulate(0x313D, body)
        actual = struct.unpack_from("<I", data, 28)[0]
        mem = data[-actual:] if actual else b""
        return ret, mem

    def continue_execution(self):
        body = struct.pack("<I", DBG_CONTINUE)
        # fire-and-forget: the target resumes and won't reliably ack
        m64 = bytearray(56)
        struct.pack_into("<I", m64, 0, API_CONTINUE)
        m64[16:16 + len(body)] = body
        self._send_data(PKT_STATE_MANIPULATE, bytes(m64), self.host_id)
        self.host_id ^= 1

    def reboot(self):
        """Request the kernel debugger's normal target reboot operation."""
        m64 = bytearray(56)
        struct.pack_into("<I", m64, 0, API_REBOOT)
        self._send_data(PKT_STATE_MANIPULATE, bytes(m64), self.host_id)
        self.host_id ^= 1


def walk_processes(kd, head_va):
    head = kd.read_virtual(head_va, 8)
    cur = struct.unpack("<Q", head)[0]           # PsActiveProcessHead.Flink
    seen = 0
    procs = []
    while cur != head_va and seen < 500:
        eproc = cur - OFF_ACTIVE_PROCESS_LINKS
        # one read covers PID(@0x1C0), Flink(@0x1C8) and ImageFileName(@0x328)
        blk = kd.read_virtual(eproc + OFF_UNIQUE_PROCESS_ID, 0x180)
        pid = struct.unpack_from("<Q", blk, 0)[0]
        flink = struct.unpack_from("<Q", blk, OFF_ACTIVE_PROCESS_LINKS - OFF_UNIQUE_PROCESS_ID)[0]
        name_off = OFF_IMAGE_FILE_NAME - OFF_UNIQUE_PROCESS_ID
        raw = blk[name_off:name_off + IMAGE_NAME_LEN]
        name = raw.split(b"\x00", 1)[0].decode("latin1", "replace")
        procs.append((pid, name, eproc))
        cur = flink
        seen += 1
    return procs


def main():
    ser = serial.Serial(require_vuart(), BAUD, timeout=0.3)
    kd = KD(ser)
    try:
        print("[*] breaking in...")
        kd.break_in()
        print("[*] broken. getting version...")
        kern_base, ps_loaded = kd.get_version()
        print("[*] KernBase        = 0x%016x" % kern_base)
        print("[*] PsLoadedModule  = 0x%016x" % ps_loaded)
        head_va = kern_base + PS_ACTIVE_PROCESS_HEAD_RVA
        print("[*] PsActiveProcessHead = 0x%016x" % head_va)
        procs = walk_processes(kd, head_va)
        print("\n%-8s %-16s %s" % ("PID", "ImageFileName", "EPROCESS"))
        print("-" * 48)
        for pid, name, eproc in procs:
            print("%-8d %-16s 0x%016x" % (pid, name, eproc))
        print("\n[*] %d processes" % len(procs))
    finally:
        print("[*] go (resuming guest)...")
        try:
            kd.continue_execution()
        except Exception as e:
            print("  continue failed: %r" % e)
        ser.close()


if __name__ == "__main__":
    main()
