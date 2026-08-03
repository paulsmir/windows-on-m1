#!/usr/bin/env python3
"""Resume an already-broken Windows KD target without issuing another break-in."""

import serial

from kd_proclist import BAUD, KD, require_vuart


with serial.Serial(require_vuart(), BAUD, timeout=0.06) as ser:
    KD(ser).continue_execution()

print("[*] KD continue sent", flush=True)
