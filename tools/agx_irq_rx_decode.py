#!/usr/bin/env python3
"""Decode the Wom1IrqRx receipt (render_admission.h ADMISSION_IRQ_RX_RECEIPT v1).

Usage: agx_irq_rx_decode.py <kmd-receipts.txt | raw .bin> [--before <kmd-receipts.txt>]

IRQ-completion step 1 census: which MessageNumber dxgkrnl gave the ASC
mailbox recv-not-empty line, how many firmware messages the ISR moved into the
ring, and how often the worker still had to pop the FIFO itself
(WorkerSyncPopHits = interrupt missed or raced).
"""
import argparse
import struct
import sys

FIELDS = ("Version", "Bytes", "ScanoutVector", "MailboxVector", "Armed",
          "IsrByMessage0", "IsrByMessage1", "IsrByMessage2", "IsrByMessageOther",
          "MailboxHandled", "MailboxMessageMask", "ScanoutHandled",
          "IsrEmptyCalls", "IsrMessages", "RingDrops", "RingConsumed",
          "WorkerSyncPops", "WorkerSyncPopHits", "RingHighWater")
FIELDS_V2 = FIELDS + ("IrqCompletionOn", "DpcQueued", "DpcWakes", "WaitEarly",
                      "WaitTimeout", "WaitSkipped")
LAYOUT = struct.Struct("<" + "I" * len(FIELDS))
LAYOUT_V2 = struct.Struct("<" + "I" * len(FIELDS_V2))
COUNTERS = FIELDS_V2[5:]
NON_COUNTERS = {"RingHighWater", "IrqCompletionOn"}


def load(path):
    data = open(path, "rb").read()
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError:
        return data
    for line in text.splitlines():
        if line.startswith("Wom1IrqRx "):
            return bytes.fromhex(line.split()[1])
    raise SystemExit(f"Wom1IrqRx not found in {path}")


def decode(blob):
    if len(blob) < LAYOUT.size:
        raise SystemExit(f"Wom1IrqRx too short: {len(blob)} bytes")
    version = struct.unpack_from("<I", blob, 0)[0]
    fields, layout = (FIELDS, LAYOUT) if version == 1 else (FIELDS_V2, LAYOUT_V2)
    if len(blob) < layout.size:
        raise SystemExit(f"Wom1IrqRx v{version} too short: {len(blob)} bytes")
    values = dict(zip(fields, layout.unpack_from(blob, 0)))
    if version not in (1, 2) or values["Bytes"] != layout.size:
        raise SystemExit(f"unsupported Wom1IrqRx version {version} size {values['Bytes']}")
    for key in FIELDS_V2:
        values.setdefault(key, 0)
    return values


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("path")
    parser.add_argument("--before")
    args = parser.parse_args()
    now = decode(load(args.path))
    if args.before:
        old = decode(load(args.before))
        for key in COUNTERS:
            if key not in NON_COUNTERS:
                now[key] -= old[key]
    for key, value in now.items():
        print(f"{key:20s} {value}")
    mask = now["MailboxMessageMask"]
    print("mailbox MessageNumbers:", [i for i in range(4) if mask & (1 << i)] or "none")
    if now["IsrMessages"]:
        share = now["WorkerSyncPopHits"] / max(1, now["IsrMessages"] + now["WorkerSyncPopHits"])
        print(f"worker self-pop share: {share:.3%}")
    waits = now["WaitEarly"] + now["WaitTimeout"]
    if waits:
        print(f"wait early share: {now['WaitEarly'] / waits:.3%} of {waits} waits, skipped {now['WaitSkipped']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
