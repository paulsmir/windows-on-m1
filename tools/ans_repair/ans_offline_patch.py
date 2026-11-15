#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""One reviewed in-place change of the Windows SYSTEM hive on the J313 internal SSD, offline.

Input is the receipt of ans_offline_recon.py.  Exactly one of two changes:
  imagepath      Services\\AppleAns\\ImagePath: the 16-digit driver-store id is replaced by the
                 old one (32 bytes of UTF-16, same length)
  startoverride  Services\\stornvme\\StartOverride value "0": DWORD 3 -> 0 (one byte)
or  restore      write a saved block back.

Log-aware change (hive not clean, or pending log entries cover the target):
  plan           offline, from saved blocks or an image (never from the proxy, never writes):
                 --target appleans-startoverride | stornvme-startoverride.  Replays the
                 transaction log (ans_hivelog.py), finds every on-disk copy of the value that any
                 prefix of the replay would read (primary file and log entry pages), changes
                 each 3 -> 0 and gives every touched log entry new Hash-1/Hash-2.  The plan file
                 lists the blocks to write with their SHA-256 before and after, and a witness
                 list: the SHA-256 of every block of the hive file and of the used part of the
                 log files.
  apply-plan     reads every witness block and every block to be written and compares them with
                 the plan; any difference stops before the first write.  Then as below.

Default is a dry run: everything is read and checked, nothing is written.  --execute writes.
Order for every affected 4 KiB block: read it, compare its SHA-256 and the bytes to be replaced
with the receipt, save it (--backup-dir, file must not exist), write, flush, read back, compare.
Any mismatch stops before the write.

Through the proxy the write uses m1n1's own nvme_write(nsid, lba, buffer), called with P_CALL at
the address taken from the image's ELF (--elf); there is no proxy NVMe write operation.  The ELF
is tied to the loaded image in two steps: offline, the image file (--macho, pinned by
--macho-sha256) must contain the ELF's code of nvme_read, nvme_write, nvme_rw_blocks and
nvme_exec_command exactly once each; at the proxy, the same code must be found in memory at
base + symbol address.  Then nvme_read called by address must return the same block as the
proxy's own NVMe read.  Only after that can nvme_write be called.

Run with `python3 -I`.  Standard library only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ans_hivelog as HL  # noqa: E402
import ans_offline_recon as RC  # noqa: E402

BLOCK = 4096
SYS_0483 = "74c4f38e2847c2db3bb34ebc5b1399203379fc0c725c1f9f083c0fd148238683"


class PatchError(Exception):
    pass


def sha256(b):
    return hashlib.sha256(b).hexdigest()


# ---- what to change, from the receipt ------------------------------------------------------
def hive_of(receipt):
    if receipt.get("tool") != "ans_offline_recon" or "error" in receipt:
        raise PatchError("not a successful ans_offline_recon receipt")
    i = receipt.get("windows_volume_index")
    if i is None:
        raise PatchError("the receipt names no Windows volume")
    vol = receipt["volumes"][i]
    hive = vol["hive"]
    base = hive["base_block"]
    if not base["clean"] or not base["checksum_ok"]:
        raise PatchError("the hive is not clean or its base block checksum is wrong: no in-place change")
    if not hive.get("bins_verified"):
        raise PatchError("the receipt does not show a verified bin walk")
    return vol, hive


def refuse_if_logged(hive, target):
    for e in hive.get("log_entries_at_or_after_hive_sequence", []):
        if target in e.get("touches", []):
            raise PatchError(f"a pending transaction-log entry (sequence {e['sequence']}) covers the target page")


def plan_imagepath(receipt, expect_sys_sha256):
    vol, hive = hive_of(receipt)
    patch = hive.get("imagepath_patch")
    ip = (hive["services"].get("AppleAns") or {}).get("ImagePath")
    if not patch or not ip:
        raise PatchError("the receipt has no AppleAns ImagePath patch description")
    if not patch["same_length"] or patch["bytes_length"] != 32:
        raise PatchError("the replacement does not have the same length")
    old, new = bytes.fromhex(patch["old_bytes_hex"]), bytes.fromhex(patch["new_bytes_hex"])
    if len(old) != 32 or len(new) != 32 or old.decode("utf-16-le") != patch["current_store_id"] or \
            new.decode("utf-16-le") != patch["replacement_store_id"]:
        raise PatchError("patch bytes do not spell the two store ids")
    if patch["current_store_id"] == patch["replacement_store_id"]:
        raise PatchError("ImagePath already names the replacement directory")
    refuse_if_logged(hive, "AppleAns_ImagePath")
    # the directory to go back to must hold the expected driver binary
    want_dir = "appleans.inf_arm64_" + patch["replacement_store_id"]
    found = None
    for pkg in vol.get("driver_store", {}).get("packages", []):
        if pkg["directory"].lower() == want_dir:
            for f in pkg["files"]:
                if f["name"].lower() == "appleans.sys":
                    found = f.get("sha256")
    if found != expect_sys_sha256:
        raise PatchError(f"{want_dir}\\AppleAns.sys: SHA-256 {found}, expected {expect_sys_sha256}")
    hashes = {p["lba"]: p["lba_sha256"] for p in ip.get("data_location", [])}
    return build_steps(patch["location"], old, new, hashes,
                       f"AppleAns ImagePath {patch['current_store_id']} -> {patch['replacement_store_id']}")


def plan_startoverride(receipt):
    _, hive = hive_of(receipt)
    so = (hive["services"].get("stornvme") or {}).get("StartOverride")
    v = next((x for x in (so or {}).get("values", []) if x["name"] == "0"), None)
    if v is None:
        raise PatchError("stornvme has no StartOverride value \"0\"")
    if v["type"] != 4 or not v["inline"] or v["data_size"] != 4 or v["data_hex"] != "03000000":
        raise PatchError(f"StartOverride \"0\" is not an inline DWORD 3 (type {v['type']}, data {v['data_hex']})")
    refuse_if_logged(hive, "stornvme_StartOverride_0")
    hashes = {p["lba"]: p["lba_sha256"] for p in v["data_location"]}
    return build_steps(v["data_location"], bytes.fromhex("03000000"), bytes(4), hashes,
                       "stornvme StartOverride \"0\": 3 -> 0")


def build_steps(location, old, new, hashes, title):
    steps, pos = [], 0
    for part in location:
        lba, off, n = part["lba"], part["offset"], part["length"]
        if off + n > BLOCK or n <= 0 or lba not in hashes:
            raise PatchError("bad location in the receipt")
        steps.append({"lba": lba, "offset": off, "old": old[pos:pos + n], "new": new[pos:pos + n],
                      "before_sha256": hashes[lba]})
        pos += n
    if pos != len(old) or len(old) != len(new) or not steps or len(steps) > 2:
        raise PatchError("location does not cover the bytes to change")
    return {"title": title, "steps": steps}


# ---- log-aware plan (offline) -----------------------------------------------------------------
TARGETS = {"appleans-startoverride": "AppleAns", "stornvme-startoverride": "stornvme"}
HIVE_PATH = "\\Windows\\System32\\config\\SYSTEM"


def describe_state(img):
    """What a boot would read from this hive image: the values that decide the storage path."""
    h = RC.Hive(lambda o, n: img[o:o + n], len(img))
    sel = h.open("Select")
    out = {"Select": {n: RC.value_text(h.value(sel, n)) for n in ("Current", "Default", "Failed", "LastKnownGood")}}
    cs = f"ControlSet{out['Select']['Current']:03d}"
    for svc in ("AppleAns", "stornvme"):
        key = h.open(f"{cs}\\Services\\{svc}")
        if key is None:
            out[svc] = None
            continue
        so = h.subkey(key, "StartOverride")
        out[svc] = {"Start": RC.value_text(h.value(key, "Start")), "ImagePath": RC.value_text(h.value(key, "ImagePath")),
                    "StartOverride": None if so is None else {v["name"]: RC.value_text(v) for v in h.values(so)}}
    return out


def locate_startoverride(service):
    def locate(img):
        h = RC.Hive(lambda o, n: img[o:o + n], len(img))
        cur = RC.value_text(h.value(h.open("Select"), "Current"))
        key = h.open(f"ControlSet{cur:03d}\\Services\\{service}\\StartOverride")
        v = h.value(key, "0") if key else None
        if v is None:
            return None
        if v["type"] != 4 or not v["inline"] or v["data_size"] != 4:
            raise HL.LogError("StartOverride \"0\" is not an inline DWORD")
        return v["data_file_offset"]
    return locate


def make_plan(disk, target, windows_partition=6, hive_path=None):
    """Plan from a read-only block source.  Returns the plan dict (JSON-serialisable)."""
    service = TARGETS[target]
    gpt = RC.parse_gpt(disk)
    part = next(p for p in gpt["partitions"] if p["index"] == windows_partition)
    vol = RC.Ntfs(disk, part["first_lba"])
    streams, files = {}, {}
    hive_path = hive_path or HIVE_PATH
    for suffix in ("", ".LOG1", ".LOG2"):
        rec = vol.resolve(hive_path + suffix)
        if rec is None:
            raise PatchError(f"{hive_path + suffix} not found")
        st = vol.stream(rec, 0x80, "")
        name = suffix or "primary"
        streams[name] = st
        files[name] = st.read(0, st.size) if st.size else b""
    primary, logs = files["primary"], {n: b for n, b in files.items() if n != "primary"}
    hive = RC.Hive(lambda o, n: primary[o:o + n], len(primary))
    if not hive.base["checksum_ok"]:
        raise PatchError("primary base block checksum is wrong")
    hive.verify_bins()
    old, new = bytes.fromhex("03000000"), bytes(4)
    locate = locate_startoverride(service)
    plan = HL.plan_byte_change(primary, logs, locate, old, new)
    checked = HL.verify_plan(primary, logs, plan, locate, old, new)
    order, notes = HL.plan_replay(primary, logs)
    # the final hive must stay structurally valid for an independent reader
    final, _ = HL.replay(plan["patched_primary"], plan["patched_logs"])
    RC.Hive(lambda o, n: final[o:o + n], len(final)).verify_bins()
    states = []
    for k in range(len(order) + 1):
        before, _ = HL.replay(primary, logs, k)
        after, _ = HL.replay(plan["patched_primary"], plan["patched_logs"], k)
        states.append({"entries_applied": k, "before": describe_state(before), "after": describe_state(after),
                       "bytes_changed_in_image": checked[k]["bytes_changed"]})
    # edits -> blocks
    per_lba = {}
    for e in plan["edits"]:
        o, n = bytes.fromhex(e["old"]), bytes.fromhex(e["new"])
        pos = 0
        for loc in RC.lbas_of(vol, streams[e["file"]], e["offset"], len(o)):
            per_lba.setdefault(loc["lba"], []).append(
                {"offset": loc["offset"], "old": o[pos:pos + loc["length"]].hex(), "new": n[pos:pos + loc["length"]].hex(),
                 "file": e["file"], "file_offset": e["offset"] + pos, "why": e["why"]})
            pos += loc["length"]
    def rank(lba):  # primary first; then log entries from the newest down, page data before hashes
        es = per_lba[lba]
        if any(x["file"] == "primary" for x in es):
            return (0, 0, 0)
        return (1, -max(x["file_offset"] for x in es), 1 if any("Hash" in x["why"] for x in es) else 0)
    blocks = []
    for lba in sorted(per_lba, key=rank):
        before = disk.read_block(lba)
        after = bytearray(before)
        for x in per_lba[lba]:
            o, n = bytes.fromhex(x["old"]), bytes.fromhex(x["new"])
            if bytes(after[x["offset"]:x["offset"] + len(o)]) != o:
                raise PatchError(f"LBA {lba}: the block does not hold the planned old bytes")
            after[x["offset"]:x["offset"] + len(n)] = n
        blocks.append({"lba": lba, "before_sha256": sha256(before), "after_sha256": sha256(bytes(after)),
                       "bytes_differing": sum(a != b for a, b in zip(before, after)), "edits": per_lba[lba]})
    # the patched files as the blocks would hold them must equal the planner's patched files
    after_by_lba = {}
    for b in blocks:
        buf = bytearray(disk.read_block(b["lba"]))
        for x in b["edits"]:
            n = bytes.fromhex(x["new"])
            buf[x["offset"]:x["offset"] + len(n)] = n
        after_by_lba[b["lba"]] = bytes(buf)
    witness = []
    for name, st in streams.items():
        if not st.size:
            continue
        used = st.size if name == "primary" else max(
            [1024] + [e["log_offset"] + e["size"] for n, e in order if n == name])
        used = min(st.size, (used + BLOCK - 1) // BLOCK * BLOCK)
        want = plan["patched_primary"] if name == "primary" else plan["patched_logs"][name]
        # the block edits, applied to the blocks, must reproduce the planner's patched file
        rebuilt = bytearray()
        for loc in RC.lbas_of(vol, st, 0, used):
            data = disk.read_block(loc["lba"])
            witness.append({"lba": loc["lba"], "sha256": sha256(data), "file": name})
            rebuilt += after_by_lba.get(loc["lba"], data)[loc["offset"]:loc["offset"] + loc["length"]]
        if bytes(rebuilt) != want[:used]:
            raise PatchError(f"internal: block edits do not reproduce the patched {name}")
    return {"tool": "ans_offline_patch", "kind": "log-aware plan", "version": 1, "target": target,
            "title": f"{service} StartOverride \"0\": 3 -> 0 in every copy the log replay can read",
            "hive": {"primary_sequence": hive.base["primary_sequence"], "secondary_sequence": hive.base["secondary_sequence"],
                     "dirty": not hive.base["clean"]},
            "log_notes": notes, "log_entries_applied": plan["entries"], "states": states,
            "locations": plan["states"], "blocks": blocks, "witness": witness}


def apply_blocks(backend, plan, backup_dir, execute, log):
    """apply-plan: witness check, per-block check, then (execute) backup, write, flush, read back."""
    backup_dir = Path(backup_dir)
    backup_dir.mkdir(parents=True, exist_ok=True)
    result = {"title": plan["title"], "executed": False, "blocks": [], "witness_blocks": len(plan["witness"])}
    if not plan["blocks"]:
        raise PatchError("the plan changes nothing")
    if len(plan["blocks"]) > 16:
        raise PatchError("the plan writes more than 16 blocks")
    bad = [w["lba"] for w in plan["witness"] if sha256(backend.read_block(w["lba"])) != w["sha256"]]
    if bad:
        raise PatchError(f"{len(bad)} witness block(s) differ from the plan (first LBA {bad[0]}): "
                         "the disk is not in the state the plan was made for")
    result["witness_ok"] = True
    planned = {w["lba"] for w in plan["witness"]}
    todo = []
    for b in plan["blocks"]:
        if b["lba"] not in planned:
            raise PatchError(f"LBA {b['lba']} is not in the witness list")
        before = backend.read_block(b["lba"])
        if sha256(before) != b["before_sha256"]:
            raise PatchError(f"LBA {b['lba']}: block differs from the plan")
        after = bytearray(before)
        for x in b["edits"]:
            o, n = bytes.fromhex(x["old"]), bytes.fromhex(x["new"])
            if len(o) != len(n) or x["offset"] + len(o) > BLOCK or bytes(after[x["offset"]:x["offset"] + len(o)]) != o:
                raise PatchError(f"LBA {b['lba']}: bytes at offset {x['offset']} are not the planned old bytes")
            after[x["offset"]:x["offset"] + len(n)] = n
        after = bytes(after)
        if sha256(after) != b["after_sha256"]:
            raise PatchError(f"LBA {b['lba']}: the patched block does not have the planned SHA-256")
        todo.append((b, before, after))
        result["blocks"].append({"lba": b["lba"], "before_sha256": b["before_sha256"], "after_sha256": b["after_sha256"],
                                 "edits": [{k: x[k] for k in ("offset", "old", "new", "why")} for x in b["edits"]]})
    result["preflight"] = backend.preflight(plan["blocks"][0]["lba"])
    log(f"PLAN {plan['title']}")
    for b in result["blocks"]:
        for x in b["edits"]:
            log(f"  LBA {b['lba']} +{x['offset']} {x['old']} -> {x['new']}  ({x['why']})")
    log(f"WITNESS {len(plan['witness'])} blocks identical to the plan")
    if not execute:
        log("DRY RUN: all checks passed, nothing written")
        return result
    for b, before, _ in todo:
        save_new(backup_dir / f"lba-{b['lba']:012d}.before.bin", before)
    for i, (b, before, after) in enumerate(todo):
        backend.write_block(b["lba"], after)
        backend.flush()
        seen = backend.read_block(b["lba"])
        result["blocks"][i]["readback_sha256"] = sha256(seen)
        if seen != after:
            result["failed_at_lba"] = b["lba"]
            raise PatchError(f"LBA {b['lba']}: read-back differs from what was written; restore every "
                             f"block already written from {backup_dir} (--mode restore), newest first")
        save_new(backup_dir / f"lba-{b['lba']:012d}.after.bin", seen)
        log(f"WROTE LBA {b['lba']} and read it back identical")
    result["executed"] = True
    return result


# ---- back ends ------------------------------------------------------------------------------
class ImageBackend:
    """A disk image file (tests and rehearsals)."""

    def __init__(self, path):
        self.f = open(path, "r+b")

    def read_block(self, lba):
        self.f.seek(lba * BLOCK)
        data = self.f.read(BLOCK)
        if len(data) != BLOCK:
            raise PatchError(f"read past the end of the image at LBA {lba}")
        return data

    def write_block(self, lba, data):
        self.f.seek(lba * BLOCK)
        self.f.write(data)

    def flush(self):
        self.f.flush()

    def preflight(self, lba):
        return {"backend": "image"}

    def close(self):
        self.f.close()


def elf_symbols(path, names):
    """{name: (value, the function's code bytes)} from an ELF64 little-endian file."""
    data = Path(path).read_bytes()
    if data[:6] != b"\x7fELF\x02\x01":
        raise PatchError("not an ELF64 little-endian file")
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum = struct.unpack_from("<HH", data, 0x3a)
    secs = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize) for i in range(shnum)]
    out = {}
    for s in secs:
        if s[1] != 2:  # SHT_SYMTAB
            continue
        strtab = secs[s[6]]
        for i in range(s[5] // 24):
            name_off, info, _, shndx, value, size = struct.unpack_from("<IBBHQQ", data, s[4] + 24 * i)
            end = data.index(b"\0", strtab[4] + name_off)
            name = data[strtab[4] + name_off:end].decode("latin-1")
            if name in names and info & 15 == 2 and 0 < shndx < shnum:
                sec = secs[shndx]
                if sec[1] != 1 or not 8 <= size <= 4096 or not sec[3] <= value <= sec[3] + sec[5] - size:
                    raise PatchError(f"{name}: not a function inside a code section")
                fo = sec[4] + value - sec[3]
                if name in out and out[name][0] != value:
                    raise PatchError(f"{name}: defined more than once")
                out[name] = (value, data[fo:fo + size])
    missing = [n for n in names if n not in out]
    if missing:
        raise PatchError(f"ELF has no function symbol {missing}")
    return out


CHECKED = ("nvme_read", "nvme_write", "nvme_rw_blocks", "nvme_exec_command")


def check_image_file(elf, macho, macho_sha256):
    """The ELF belongs to the image file whose hash names the bench m1n1: same code bytes."""
    raw = Path(macho).read_bytes()
    if sha256(raw) != macho_sha256:
        raise PatchError("m1n1 image file does not have the expected SHA-256")
    for name, (_, code) in elf_symbols(elf, CHECKED).items():
        if raw.count(code) != 1:
            raise PatchError(f"{name}: the ELF's code is not in the image file exactly once")


class ProxyBackend:
    """m1n1 proxy.  Reads: P_NVME_READ.  Write: P_CALL of the image's nvme_write.  `proxy`,
    `iface` and `utils` are the proxyclient objects (or test doubles with the same methods)."""

    def __init__(self, proxy, iface, utils, elf):
        self.p, self.iface, self.u = proxy, iface, utils
        self.sym = elf_symbols(elf, CHECKED)
        self.bfr = self.scratch = None
        self.up = False
        try:
            self.base = self.p.get_base()
            self.bfr = self.u.memalign(0x4000, BLOCK)
            self.scratch = self.u.memalign(0x4000, BLOCK)
            rc = self.p.nvme_init()
            if rc != 1:
                raise PatchError(f"nvme_init returned {rc}")
            self.up = True
        except BaseException:
            self.close()
            raise

    def read_block(self, lba):
        rc = self.p.nvme_read(1, lba, self.bfr)
        if rc != 1:
            raise PatchError(f"nvme_read LBA {lba} returned {rc}")
        return bytes(self.iface.readmem(self.bfr, BLOCK))

    def preflight(self, lba):
        """Proves the image and the calling convention without writing anything."""
        for name, (value, code) in self.sym.items():
            if bytes(self.iface.readmem(self.base + value, len(code))) != code:
                raise PatchError(f"{name}: code in memory differs from the ELF; wrong image")
        direct = self.read_block(lba)
        self.iface.writemem(self.scratch, b"\xa5" * BLOCK)
        rc = self.p.call(self.base + self.sym["nvme_read"][0], 1, lba, self.scratch) & 0xff
        called = bytes(self.iface.readmem(self.scratch, BLOCK))
        if rc != 1 or called != direct:
            raise PatchError("nvme_read called by address does not return the proxy's own read")
        return {"backend": "proxy", "base": hex(self.base),
                "nvme_read": hex(self.sym["nvme_read"][0]), "nvme_write": hex(self.sym["nvme_write"][0])}

    def write_block(self, lba, data):
        if len(data) != BLOCK:
            raise PatchError("block size")
        self.iface.writemem(self.scratch, data)
        if bytes(self.iface.readmem(self.scratch, BLOCK)) != data:
            raise PatchError("the block did not reach m1n1 memory intact; nothing was written")
        rc = self.p.call(self.base + self.sym["nvme_write"][0], 1, lba, self.scratch) & 0xff
        if rc != 1:
            raise PatchError(f"nvme_write LBA {lba} returned {rc}")

    def flush(self):
        rc = self.p.nvme_flush(1)
        if rc != 1:
            raise PatchError(f"nvme_flush returned {rc}")

    def close(self):
        try:
            if self.up:
                self.p.nvme_shutdown()
                self.up = False
        finally:
            for b in (self.bfr, self.scratch):
                if b is not None:
                    self.u.free(b)


# ---- the change -----------------------------------------------------------------------------
def save_new(path, data):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        os.write(fd, data)
        os.fsync(fd)
    finally:
        os.close(fd)


def apply(backend, plan, backup_dir, execute, log):
    """Checks every block, then (execute) backs up, writes and verifies them one by one."""
    backup_dir = Path(backup_dir)
    backup_dir.mkdir(parents=True, exist_ok=True)
    result = {"title": plan["title"], "executed": False, "blocks": []}
    blocks = []
    for st in plan["steps"]:
        before = backend.read_block(st["lba"])
        if sha256(before) != st["before_sha256"]:
            raise PatchError(f"LBA {st['lba']}: block differs from the one the reconnaissance saw")
        if before[st["offset"]:st["offset"] + len(st["old"])] != st["old"]:
            raise PatchError(f"LBA {st['lba']}: bytes at offset {st['offset']} are not the expected old bytes")
        after = before[:st["offset"]] + st["new"] + before[st["offset"] + len(st["new"]):]
        if len(after) != BLOCK or sum(a != b for a, b in zip(before, after)) > len(st["new"]):
            raise PatchError("internal: patched block is not a same-size change")
        blocks.append((st, before, after))
        result["blocks"].append({"lba": st["lba"], "offset": st["offset"], "old": st["old"].hex(),
                                 "new": st["new"].hex(), "before_sha256": sha256(before),
                                 "after_sha256": sha256(after)})
    result["preflight"] = backend.preflight(plan["steps"][0]["lba"])
    log(f"PLAN {plan['title']}: " + ", ".join(
        f"LBA {b['lba']} +{b['offset']} {b['old']} -> {b['new']}" for b in result["blocks"]))
    if not execute:
        log("DRY RUN: all checks passed, nothing written")
        return result
    for st, before, _ in blocks:  # every backup exists before the first write
        save_new(backup_dir / f"lba-{st['lba']:012d}.before.bin", before)
    for i, (st, before, after) in enumerate(blocks):
        backend.write_block(st["lba"], after)
        backend.flush()
        seen = backend.read_block(st["lba"])
        result["blocks"][i]["readback_sha256"] = sha256(seen)
        if seen != after:
            result["failed_at_lba"] = st["lba"]
            raise PatchError(f"LBA {st['lba']}: read-back differs from what was written; "
                             f"restore with --mode restore and {backup_dir}")
        save_new(backup_dir / f"lba-{st['lba']:012d}.after.bin", seen)
        log(f"WROTE LBA {st['lba']} and read it back identical")
    result["executed"] = True
    return result


def restore(backend, backup_dir, lba, expect_current_sha256, execute, log):
    before = Path(backup_dir, f"lba-{lba:012d}.before.bin").read_bytes()
    if len(before) != BLOCK:
        raise PatchError("backup block has the wrong size")
    now = backend.read_block(lba)
    if expect_current_sha256 and sha256(now) != expect_current_sha256:
        raise PatchError("the block on disk is not the one named by --expect-current-sha256")
    pre = backend.preflight(lba)
    log(f"RESTORE LBA {lba}: {sha256(now)} -> {sha256(before)}")
    result = {"title": f"restore LBA {lba}", "executed": False, "preflight": pre,
              "current_sha256": sha256(now), "backup_sha256": sha256(before)}
    if execute and now != before:
        backend.write_block(lba, before)
        backend.flush()
        if backend.read_block(lba) != before:
            raise PatchError("read-back after restore differs")
        result["executed"] = True
    return result


def main_plan(a, ap, log):
    if not a.target or bool(a.blocks) == bool(a.image) or a.proxyclient or a.execute:
        ap.error("plan needs --target and exactly one of --blocks/--image, and never --proxyclient/--execute")
    dev = RC.BlocksDevice(a.blocks) if a.blocks else RC.ImageDevice(a.image)
    try:
        plan = make_plan(RC.Disk(dev), a.target, a.windows_partition)
    except (PatchError, HL.LogError, RC.ReconError, StopIteration) as e:
        print(f"PLAN_ERROR {type(e).__name__}: {e}", flush=True)
        return 3
    finally:
        dev.close()
    a.output.parent.mkdir(parents=True, exist_ok=True)
    raw = json.dumps(plan, indent=1).encode()
    a.output.write_bytes(raw)
    log(f"HIVE primary={plan['hive']['primary_sequence']} secondary={plan['hive']['secondary_sequence']} "
        f"dirty={plan['hive']['dirty']}; log entries applied: " +
        (", ".join(f"{e['sequence']}({e['pages']} pages, {e['file']})" for e in plan["log_entries_applied"]) or "none"))
    for st in plan["states"]:
        b, c = st["before"], st["after"]
        svc = TARGETS[a.target]
        log(f"STATE after {st['entries_applied']} entries: {svc} StartOverride {b[svc] and b[svc]['StartOverride']} -> "
            f"{c[svc] and c[svc]['StartOverride']}; image bytes changed {st['bytes_changed_in_image']}")
    for b in plan["blocks"]:
        for x in b["edits"]:
            log(f"WRITE LBA {b['lba']} +{x['offset']} {x['old']} -> {x['new']}  ({x['file']} @{x['file_offset']}: {x['why']})")
    log(f"PLAN_OK blocks_to_write={len(plan['blocks'])} witness_blocks={len(plan['witness'])} "
        f"sha256={sha256(raw)} -> {a.output}")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="One in-place change of the offline Windows SYSTEM hive (dry run by default)")
    ap.add_argument("--mode", required=True, choices=("imagepath", "startoverride", "restore", "plan", "apply-plan"))
    ap.add_argument("--target", choices=sorted(TARGETS), help="plan: the value to change")
    ap.add_argument("--blocks", type=Path, help="plan: directory of saved blocks (read-only source)")
    ap.add_argument("--plan", type=Path, help="apply-plan: the plan file")
    ap.add_argument("--plan-sha256", help="apply-plan: expected SHA-256 of the plan file (required)")
    ap.add_argument("--windows-partition", type=int, default=6, help="plan: GPT entry index of the Windows volume")
    ap.add_argument("--receipt", type=Path, help="ans_offline_recon receipt (imagepath, startoverride)")
    ap.add_argument("--receipt-sha256", help="expected SHA-256 of the receipt file")
    src = ap.add_mutually_exclusive_group()
    src.add_argument("--image", type=Path)
    src.add_argument("--proxyclient", type=Path)
    ap.add_argument("--elf", type=Path, help="m1n1.elf of the image at the proxy (required with --proxyclient)")
    ap.add_argument("--elf-sha256", help="expected SHA-256 of the ELF")
    ap.add_argument("--macho", type=Path, help="the m1n1 image file that was loaded (required with --proxyclient)")
    ap.add_argument("--macho-sha256", help="its SHA-256 (required with --proxyclient)")
    ap.add_argument("--backup-dir", type=Path, help="required for every mode except plan")
    ap.add_argument("--output", type=Path, required=True, help="JSON result (must not exist)")
    ap.add_argument("--expect-sys-sha256", default=SYS_0483)
    ap.add_argument("--lba", type=int, help="restore: block to write back")
    ap.add_argument("--expect-current-sha256", help="restore: the block now on disk")
    ap.add_argument("--execute", action="store_true", help="write (default: dry run)")
    a = ap.parse_args(argv)
    if a.output.exists():
        ap.error("refusing to overwrite an existing result")
    log = lambda m: print(m, flush=True)
    if a.mode == "plan":
        return main_plan(a, ap, log)
    if not a.backup_dir or not (a.image or a.proxyclient):
        ap.error("--backup-dir and one of --image/--proxyclient are required")
    result, backend, code = {"tool": "ans_offline_patch", "mode": a.mode, "execute": a.execute}, None, 0
    try:
        plan = block_plan = None
        if a.mode == "apply-plan":
            if not a.plan or not a.plan_sha256:
                raise PatchError("--plan and --plan-sha256 are required")
            raw = a.plan.read_bytes()
            if sha256(raw) != a.plan_sha256:
                raise PatchError("plan file does not have the expected SHA-256")
            result["plan_sha256"] = sha256(raw)
            block_plan = json.loads(raw)
            if block_plan.get("kind") != "log-aware plan" or block_plan.get("version") != 1:
                raise PatchError("not a log-aware plan of this tool")
        elif a.mode != "restore":
            if not a.receipt:
                raise PatchError("--receipt is required")
            raw = a.receipt.read_bytes()
            if a.receipt_sha256 and sha256(raw) != a.receipt_sha256:
                raise PatchError("receipt file does not have the expected SHA-256")
            result["receipt_sha256"] = sha256(raw)
            receipt = json.loads(raw)
            plan = plan_imagepath(receipt, a.expect_sys_sha256) if a.mode == "imagepath" else plan_startoverride(receipt)
        elif a.lba is None:
            raise PatchError("--lba is required for restore")
        if a.image:
            backend = ImageBackend(a.image)
        else:
            if not a.elf or not a.macho or not a.macho_sha256:
                raise PatchError("--elf, --macho and --macho-sha256 are required with --proxyclient")
            check_image_file(a.elf, a.macho, a.macho_sha256)
            if a.elf_sha256 and sha256(a.elf.read_bytes()) != a.elf_sha256:
                raise PatchError("ELF does not have the expected SHA-256")
            sys.path.insert(0, str(a.proxyclient.resolve()))
            from m1n1.proxy import UartInterface, M1N1Proxy  # noqa: E402
            from m1n1.proxyutils import ProxyUtils, bootstrap_port  # noqa: E402
            iface = UartInterface()
            p = M1N1Proxy(iface, debug=False)
            bootstrap_port(iface, p)
            backend = ProxyBackend(p, iface, ProxyUtils(p), a.elf)
        if block_plan:
            result.update(apply_blocks(backend, block_plan, a.backup_dir, a.execute, log))
        elif plan:
            result.update(apply(backend, plan, a.backup_dir, a.execute, log))
        else:
            result.update(restore(backend, a.backup_dir, a.lba, a.expect_current_sha256, a.execute, log))
    except (PatchError, HL.LogError, RC.ReconError, OSError, KeyError, ValueError) as e:
        result["error"] = f"{type(e).__name__}: {e}"
        code = 3
    finally:
        if backend:
            backend.close()
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=1))
    print(("PATCH_ERROR " + result["error"]) if code else
          ("PATCH_DONE" if result.get("executed") else "PATCH_DRY_RUN_OK") + f" -> {a.output}", flush=True)
    return code


if __name__ == "__main__":
    sys.exit(main())
