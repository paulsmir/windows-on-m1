#!/usr/bin/env python3
"""Decisive KDCOM handshake probe: inspect the state-change packet, then test whether an
ACK stops the target's re-send flood. Always sends a continue at the end."""
import os
import serial, struct, time

PORT = os.environ.get("M1N1VUART")
if not PORT:
    raise SystemExit("set M1N1VUART to the guest virtual-UART device")
ser = serial.Serial(PORT, 115200, timeout=0.3)


def recv(timeout=3.0):
    deadline = time.time() + timeout
    win = b""
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            continue
        win = (win + b)[-4:]
        if len(win) == 4 and struct.unpack("<I", win)[0] in (0x30303030, 0x69696969):
            lead = struct.unpack("<I", win)[0]
            hdr = ser.read(12)
            if len(hdr) < 12:
                return None
            ptype, count, pid, csum = struct.unpack("<HHII", hdr)
            data = b""
            if lead == 0x30303030:
                while len(data) < count and time.time() < deadline:
                    data += ser.read(count - len(data))
                ser.read(1)  # trailer
            return (lead, ptype, count, pid, csum, data)
    return None


def send_ctrl(ptype, pid):
    ser.write(struct.pack("<IHHII", 0x69696969, ptype, 0, pid, 0))
    ser.flush()


print("[*] sending breakin")
for _ in range(3):
    ser.write(b"\x62"); ser.flush(); time.sleep(0.05)

# 1) grab one state change, show it fully
sc = None
for _ in range(20):
    p = recv()
    if p and p[1] == 7:
        sc = p
        break
if not sc:
    print("no state change"); ser.close(); raise SystemExit
lead, ptype, count, pid, csum, data = sc
print("[*] STATE_CHANGE64: type=%d count=%d pid=0x%08x csum=0x%08x" % (ptype, count, pid, csum))
print("    first 32 data bytes: %s" % data[:32].hex())
print("    NewState(u32@0)=0x%x" % struct.unpack_from("<I", data, 0)[0])

# 2) ACK it, then see if the flood stops
print("[*] sending ACK pid=0x%08x, then listening 4s..." % pid)
send_ctrl(4, pid)
after = 0
t = time.time()
while time.time() - t < 4.0:
    p = recv(timeout=1.0)
    if p and p[1] == 7:
        after += 1
print("[*] state-changes received AFTER ack: %d  (0 => ACK accepted)" % after)

# 3) try ACK with a few id variants if still flooding
for tid in (0x80800801, 0x00000800, 0x80800000):
    send_ctrl(4, tid)
    time.sleep(0.2)
    cnt = 0
    t = time.time()
    while time.time() - t < 1.5:
        p = recv(timeout=0.8)
        if p and p[1] == 7:
            cnt += 1
    print("    ack id=0x%08x -> %d state-changes after" % (tid, cnt))

# resume
print("[*] continue")
m64 = bytearray(56)
struct.pack_into("<I", m64, 0, 0x313C)
struct.pack_into("<I", m64, 16, 0x00010002)
ser.write(struct.pack("<IHHII", 0x30303030, 2, 56, 0x80800800, sum(m64) & 0xffffffff) + bytes(m64) + b"\xaa")
ser.flush()
ser.close()
