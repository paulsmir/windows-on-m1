#!/usr/bin/env python3
"""Probe whether the Windows KD target is alive, always resuming it afterwards."""

import signal

import serial

from kd_proclist import BAUD, KD, require_vuart


class ProbeTimeout(Exception):
    pass


def alarm_handler(_signum, _frame):
    raise ProbeTimeout("KD liveness probe exceeded 15 seconds")


signal.signal(signal.SIGALRM, alarm_handler)
signal.alarm(15)

ser = serial.Serial(require_vuart(), BAUD, timeout=0.25)
kd = KD(ser)
try:
    print("[*] sending KD break-in", flush=True)
    state = kd.break_in()
    print("[+] Windows kernel answered STATE_CHANGE64 (%d bytes)" % len(state), flush=True)
except Exception as exc:
    print("[-] no usable KD response: %s" % exc, flush=True)
    raise
finally:
    signal.alarm(0)
    try:
        kd.continue_execution()
        ser.flush()
        print("[*] KD continue sent", flush=True)
    finally:
        ser.close()
