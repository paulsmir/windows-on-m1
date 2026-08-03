#!/usr/bin/env python3
"""
Capture the guest's own UART.

Kept as a file rather than a heredoc inside run-with-uart.sh: the heredoc fed the script on
stdin, and running that under nohup left the interpreter with nothing to read, so it exited
at once and the log stayed empty.

Order matters at the call site. hv_vuart drops a byte when the host is not holding the
secondary ACM device open, and the firmware prints during PrePi, so this has to be running
before the guest starts.
"""
import serial, sys, time

dev = sys.argv[1]
limit = float(sys.argv[2]) if len(sys.argv) > 2 else 2400

s = serial.Serial(dev, 115200, timeout=0.5)

# guest-uart.tlog carries a host timestamp per line. The guest's own clock is one of the
# things under suspicion here, so its log cannot be used to measure itself.
with open('guest-uart.log', 'wb', buffering=0) as f, \
     open('guest-uart.tlog', 'w', buffering=1) as tf:
    t0 = time.time()
    pending = b''
    while time.time() - t0 < limit:
        c = s.read(8192)
        if not c:
            continue
        f.write(c)
        pending += c
        while b'\n' in pending:
            line, pending = pending.split(b'\n', 1)
            tf.write(f"{time.time() - t0:9.3f}  {line.decode('utf-8', 'replace')}\n")
