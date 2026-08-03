#!/usr/bin/env python3
"""Reboot the live Windows target through its serial kernel-debug transport."""

import time

import serial

from kd_proclist import BAUD, KD, require_vuart


with serial.Serial(require_vuart(), BAUD, timeout=0.06) as ser:
    kd = KD(ser)
    print("[*] breaking into Windows...", flush=True)
    kd.break_in()
    print("[*] sending KD reboot...", flush=True)
    kd.reboot()
    time.sleep(1)
