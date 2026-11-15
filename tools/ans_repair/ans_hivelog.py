#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Registry hive transaction logs (new format, Windows 8.1+): parse, verify, replay, and plan a
value change that survives Windows' own log replay.

Format source: Maxim Suhanov, "Windows registry file format specification"
(github.com/msuhanov/regf), sections "Format of transaction log files > New format",
"Dirty state of a hive" and "Multiple transaction log files > New format".  Rules used here, by
that document's note numbers under "Log entries":
  1-3  Hash-1 = Marvin32 over the entry from offset 40 for Size-40 bytes; Hash-2 = Marvin32 over
       the first 32 bytes of the entry (so it covers Hash-1); seed 82 EF 4D 88 7A 4E 55 C5 read
       as the number 0x82EF4D887A4E55C5.  (Hash-2 checked here against entries written by
       Windows on the J313 bench disk.)
  5    a dirty primary with a valid base block: entries with a sequence number >= the log base
       block's primary sequence number are applied
  7    the first entry must carry exactly that number and it must not be below the primary
       file's secondary sequence number; an entry N not followed by N+1 ends the replay
  8    a wrong Hash-1, Hash-2 or hive-bins size ends the replay before that entry
The specification does not say what the sequence numbers become after recovery; replay() sets
both to last applied + 1 in its OUTPUT image only (nothing here writes a disk).

Standard library only.
"""
from __future__ import annotations

import struct

PAGE = 4096
SEED = 0x82EF4D887A4E55C5
M = 0xffffffff


class LogError(Exception):
    pass


def marvin32(data, seed=SEED):
    """64-bit result as stored in a log entry (low word first)."""
    p0, p1 = seed & M, seed >> 32

    def rotl(x, r):
        return ((x << r) | (x >> (32 - r))) & M

    def block(p0, p1):
        p1 ^= p0
        p0 = rotl(p0, 20)
        p0 = (p0 + p1) & M
        p1 = rotl(p1, 9)
        p1 ^= p0
        p0 = rotl(p0, 27)
        p0 = (p0 + p1) & M
        p1 = rotl(p1, 19)
        return p0, p1

    n = len(data) & ~3
    for (v,) in struct.iter_unpack("<I", data[:n]):
        p0, p1 = block((p0 + v) & M, p1)
    final = 0x80
    for b in reversed(data[n:]):
        final = final << 8 | b
    p0, p1 = block((p0 + final) & M, p1)
    p0, p1 = block(p0, p1)
    return struct.pack("<II", p0, p1)


def checksum(base):
    x = 0
    for (v,) in struct.iter_unpack("<I", base[:508]):
        x ^= v
    return 0xfffffffe if x == 0xffffffff else 1 if x == 0 else x


def base_info(b):
    if len(b) < 512 or b[:4] != b"regf":
        return None
    seq1, seq2 = struct.unpack_from("<II", b, 4)
    return {"primary": seq1, "secondary": seq2, "type": struct.unpack_from("<I", b, 28)[0],
            "bins": struct.unpack_from("<I", b, 40)[0],
            "checksum_ok": struct.unpack_from("<I", b, 508)[0] == checksum(b)}


def parse_entries(log):
    """Every structurally plausible entry from offset 512 on, with its hash verdicts.  Stops at
    the first position that is not an entry.  No sequence rule is applied here."""
    out, pos = [], 512
    while pos + 40 <= len(log) and log[pos:pos + 4] == b"HvLE":
        size, flags, seq, bins, pages = struct.unpack_from("<5I", log, pos + 4)
        if size < 40 or size % 512 or pos + size > len(log) or 40 + 8 * pages > size:
            break
        refs, at = [], pos + 40 + 8 * pages
        ok = True
        for i in range(pages):
            off, length = struct.unpack_from("<II", log, pos + 40 + 8 * i)
            if length == 0 or length % PAGE or off % PAGE or at + length > pos + size:
                ok = False
                break
            refs.append({"offset": off, "size": length, "log_offset": at})
            at += length
        if not ok:
            break
        out.append({"log_offset": pos, "size": size, "flags": flags, "sequence": seq, "bins": bins,
                    "refs": refs,
                    "hash1_ok": marvin32(log[pos + 40:pos + size]) == log[pos + 24:pos + 32],
                    "hash2_ok": marvin32(log[pos:pos + 32]) == log[pos + 32:pos + 40]})
        pos += size
    return out


def usable_entries(primary, log):
    """The entries Windows would apply from one log file, and why the list ends."""
    pb, lb = base_info(primary), base_info(log)
    if lb is None:
        return [], "no log base block"
    if lb["type"] != 6 or not lb["checksum_ok"]:
        return [], "log base block is not a valid new-format log header"
    want, out = lb["primary"], []
    if pb and pb["checksum_ok"] and want < pb["secondary"]:
        return [], "log starts below the primary file's secondary sequence number"
    for e in parse_entries(log):
        if e["sequence"] != want:
            return out, f"entry at {e['log_offset']} has sequence {e['sequence']}, expected {want}"
        if not e["hash1_ok"] or not e["hash2_ok"] or e["bins"] % PAGE:
            return out, f"entry {e['sequence']} fails Hash-1/Hash-2/bins-size"
        out.append(e)
        want += 1
    return out, "no further entry"


def plan_replay(primary, logs):
    """[(log name, entry)] in the order of application, and notes.  `logs` maps a name to the
    log file's bytes."""
    pb = base_info(primary)
    if pb is None:
        raise LogError("primary file has no base block")
    if not pb["checksum_ok"]:
        raise LogError("primary base block checksum is wrong: recovery from the log's base block is not modelled")
    notes = {}
    if pb["primary"] == pb["secondary"]:
        return [], {"primary": "clean: log entries are ignored"}
    runs = []
    for name, log in logs.items():
        if len(log) < 1024:
            notes[name] = "empty"
            continue
        ents, why = usable_entries(primary, log)
        notes[name] = why
        if ents:
            runs.append((ents[0]["sequence"], name, ents))
    runs.sort()
    order, last = [], None
    for _, name, ents in runs:
        for e in ents:
            if last is not None and e["sequence"] != last + 1:
                if e["sequence"] <= last:
                    continue
                notes[name] += f"; stops: sequence {e['sequence']} does not follow {last}"
                break
            order.append((name, e))
            last = e["sequence"]
    return order, notes


def replay(primary, logs, count=None):
    """(image, source) after applying the first `count` usable entries (all when None).
    source maps a hive-bins page offset to (log name, log file offset of that page's copy) for
    every page that came from a log; pages not in it come from the primary file."""
    order, _ = plan_replay(primary, logs)
    if count is not None:
        order = order[:count]
    img, source = bytearray(primary), {}
    for name, e in order:
        log = logs[name]
        if len(img) < PAGE + e["bins"]:
            img += bytes(PAGE + e["bins"] - len(img))
        for r in e["refs"]:
            img[PAGE + r["offset"]:PAGE + r["offset"] + r["size"]] = log[r["log_offset"]:r["log_offset"] + r["size"]]
            for k in range(0, r["size"], PAGE):
                source[r["offset"] + k] = (name, r["log_offset"] + k)
        struct.pack_into("<I", img, 40, e["bins"])
        struct.pack_into("<I", img, 144, struct.unpack_from("<I", img, 144)[0] & ~1 | e["flags"] & 1)
    if order:
        seq = order[-1][1]["sequence"] + 1
        struct.pack_into("<II", img, 4, seq, seq)
        struct.pack_into("<I", img, 508, checksum(bytes(img[:512])))
    return bytes(img), source


def plan_byte_change(primary, logs, locate, old, new):
    """Edits that make a value read `new` after ANY prefix of the log replay.

    locate(image) -> file offset of the bytes in that hive image, or None when the value does
    not exist there.  For every prefix (0..n entries applied) the bytes are located in that
    state and traced to the copy they come from (primary file or one log entry's page).  Every
    distinct copy is changed and each touched entry gets new Hash-1 and Hash-2.

    Returns {"edits": [{"file": name or "primary", "offset": file offset, "old", "new", "why"}],
             "states": [...]}; raises LogError when a located copy holds neither old nor new."""
    if len(old) != len(new) or not old:
        raise LogError("old and new must have the same non-zero length")
    order, notes = plan_replay(primary, logs)
    copies, states = {}, []
    for k in range(len(order) + 1):
        img, source = replay(primary, logs, k)
        pos = locate(img)
        st = {"entries_applied": k, "last_sequence": order[k - 1][1]["sequence"] if k else None, "file_offset": pos}
        if pos is not None:
            page = (pos - PAGE) & ~(PAGE - 1)
            if (pos - PAGE + len(old) - 1) & ~(PAGE - 1) != page:
                raise LogError("the bytes straddle a page")
            have = img[pos:pos + len(old)]
            st["bytes"] = have.hex()
            name, at = source.get(page, ("primary", None))
            where = pos if name == "primary" else at + (pos - PAGE - page)
            st["copy"] = {"file": name, "offset": where}
            if have == old:
                copies.setdefault((name, where), k)
            elif have != new:
                raise LogError(f"after {k} entries the value holds {have.hex()}, neither old nor new")
        states.append(st)
    edits = [{"file": name, "offset": off, "old": old.hex(), "new": new.hex(),
              "why": f"copy read after {k} log entries"} for (name, off), k in sorted(copies.items(), key=lambda x: x[1])]
    # new hashes for every entry that holds a changed copy
    patched = {name: bytearray(log) for name, log in logs.items()}
    for e in edits:
        if e["file"] != "primary":
            patched[e["file"]][e["offset"]:e["offset"] + len(new)] = new
    for name, ent in order:
        lo, hi = ent["log_offset"], ent["log_offset"] + ent["size"]
        if not any(e["file"] == name and lo <= e["offset"] < hi for e in edits):
            continue
        buf = patched[name]
        h1_old, h2_old = bytes(buf[lo + 24:lo + 32]), bytes(buf[lo + 32:lo + 40])
        h1 = marvin32(bytes(buf[lo + 40:hi]))
        buf[lo + 24:lo + 32] = h1
        h2 = marvin32(bytes(buf[lo:lo + 32]))
        buf[lo + 32:lo + 40] = h2
        edits.append({"file": name, "offset": lo + 24, "old": (h1_old + h2_old).hex(), "new": (h1 + h2).hex(),
                      "why": f"Hash-1 and Hash-2 of log entry {ent['sequence']}"})
    new_primary = bytearray(primary)
    for e in edits:
        if e["file"] == "primary":
            new_primary[e["offset"]:e["offset"] + len(new)] = new
    return {"edits": edits, "states": states, "notes": notes,
            "entries": [{"file": n, "sequence": e["sequence"], "log_offset": e["log_offset"], "size": e["size"],
                         "pages": sum(r["size"] for r in e["refs"]) // PAGE} for n, e in order],
            "patched_primary": bytes(new_primary), "patched_logs": {n: bytes(b) for n, b in patched.items()}}


def verify_plan(primary, logs, plan, locate, old, new):
    """Independent re-check of a plan: the patched files replay with the same entries, every
    prefix reads `new` wherever the unpatched one read `old`, and each prefix image differs from
    the unpatched prefix image only in those bytes (plus nothing else).  Returns a summary."""
    p2, l2 = plan["patched_primary"], plan["patched_logs"]
    o1, _ = plan_replay(primary, logs)
    o2, _ = plan_replay(p2, l2)
    if [(n, e["sequence"], e["log_offset"], e["size"]) for n, e in o1] != \
            [(n, e["sequence"], e["log_offset"], e["size"]) for n, e in o2]:
        raise LogError("the patched log does not replay the same entries")
    out = []
    for k in range(len(o1) + 1):
        a, _ = replay(primary, logs, k)
        b, _ = replay(p2, l2, k)
        pos = locate(b)
        if pos != locate(a) or len(a) != len(b):
            raise LogError(f"prefix {k}: the value moved or the image size changed")
        diff = []
        for o in range(0, len(a), PAGE):
            if a[o:o + PAGE] != b[o:o + PAGE]:
                diff += [o + i for i in range(len(a[o:o + PAGE])) if a[o + i] != b[o + i]]
        if pos is None:
            if diff:
                raise LogError(f"prefix {k}: no value, yet the images differ")
            out.append({"entries_applied": k, "value": None, "bytes_changed": 0})
            continue
        if b[pos:pos + len(new)] != new:
            raise LogError(f"prefix {k}: value still reads {b[pos:pos + len(new)].hex()}")
        allowed = set(range(pos, pos + len(new)))
        if not set(diff) <= allowed:
            raise LogError(f"prefix {k}: bytes outside the value changed")
        out.append({"entries_applied": k, "value": new.hex(), "bytes_changed": len(diff)})
    return out
