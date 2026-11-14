#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""One reviewed in-place change of the Windows SYSTEM hive on the J313 internal SSD, offline.

Input is the receipt of ans_offline_recon.py.  Exactly one of two changes:
  imagepath      Services\\AppleAns\\ImagePath: the 16-digit driver-store id is replaced by the
                 old one (32 bytes of UTF-16, same length)
  startoverride  Services\\stornvme\\StartOverride value "0": DWORD 3 -> 0 (one byte)
or  restore      write a saved block back.

Default is a dry run: everything is read and checked, nothing is written.  --execute writes.
Order for every affected 4 KiB block: read it, compare its SHA-256 and the bytes to be replaced
with the receipt, save it (--backup-dir, file must not exist), write, flush, read back, compare.
Any mismatch stops before the write.

Through the proxy the write uses m1n1's own nvme_write(nsid, lba, buffer), called with P_CALL at
the address taken from the image's ELF (--elf); there is no proxy NVMe write operation.  Before
any write the tool proves it has the right image and calling convention: the code bytes of
nvme_read and nvme_write in memory equal the ELF's, and nvme_read called the same way returns
the same block as the proxy's own NVMe read.

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
    """{name: (value, first 64 code bytes)} from an ELF64 little-endian file."""
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
            name_off, info, _, shndx, value, _ = struct.unpack_from("<IBBHQQ", data, s[4] + 24 * i)
            end = data.index(b"\0", strtab[4] + name_off)
            name = data[strtab[4] + name_off:end].decode("latin-1")
            if name in names and info & 15 == 2 and 0 < shndx < shnum:
                sec = secs[shndx]
                if sec[1] != 1 or not sec[3] <= value < sec[3] + sec[5] - 64:
                    raise PatchError(f"{name}: not inside a code section")
                fo = sec[4] + value - sec[3]
                out[name] = (value, data[fo:fo + 64])
    missing = [n for n in names if n not in out]
    if missing:
        raise PatchError(f"ELF has no function symbol {missing}")
    return out


class ProxyBackend:
    """m1n1 proxy.  Reads: P_NVME_READ.  Write: P_CALL of the image's nvme_write.  `proxy`,
    `iface` and `utils` are the proxyclient objects (or test doubles with the same methods)."""

    def __init__(self, proxy, iface, utils, elf):
        self.p, self.iface, self.u = proxy, iface, utils
        self.sym = elf_symbols(elf, ("nvme_read", "nvme_write"))
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
            if bytes(self.iface.readmem(self.base + value, 64)) != code:
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


def main(argv=None):
    ap = argparse.ArgumentParser(description="One in-place change of the offline Windows SYSTEM hive (dry run by default)")
    ap.add_argument("--mode", required=True, choices=("imagepath", "startoverride", "restore"))
    ap.add_argument("--receipt", type=Path, help="ans_offline_recon receipt (imagepath, startoverride)")
    ap.add_argument("--receipt-sha256", help="expected SHA-256 of the receipt file")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--image", type=Path)
    src.add_argument("--proxyclient", type=Path)
    ap.add_argument("--elf", type=Path, help="m1n1.elf of the image at the proxy (required with --proxyclient)")
    ap.add_argument("--elf-sha256", help="expected SHA-256 of the ELF")
    ap.add_argument("--backup-dir", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True, help="JSON result (must not exist)")
    ap.add_argument("--expect-sys-sha256", default=SYS_0483)
    ap.add_argument("--lba", type=int, help="restore: block to write back")
    ap.add_argument("--expect-current-sha256", help="restore: the block now on disk")
    ap.add_argument("--execute", action="store_true", help="write (default: dry run)")
    a = ap.parse_args(argv)
    if a.output.exists():
        ap.error("refusing to overwrite an existing result")
    log = lambda m: print(m, flush=True)
    result, backend, code = {"tool": "ans_offline_patch", "mode": a.mode, "execute": a.execute}, None, 0
    try:
        plan = None
        if a.mode != "restore":
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
            if not a.elf:
                raise PatchError("--elf is required with --proxyclient")
            if a.elf_sha256 and sha256(a.elf.read_bytes()) != a.elf_sha256:
                raise PatchError("ELF does not have the expected SHA-256")
            sys.path.insert(0, str(a.proxyclient.resolve()))
            from m1n1.proxy import UartInterface, M1N1Proxy  # noqa: E402
            from m1n1.proxyutils import ProxyUtils, bootstrap_port  # noqa: E402
            iface = UartInterface()
            p = M1N1Proxy(iface, debug=False)
            bootstrap_port(iface, p)
            backend = ProxyBackend(p, iface, ProxyUtils(p), a.elf)
        if plan:
            result.update(apply(backend, plan, a.backup_dir, a.execute, log))
        else:
            result.update(restore(backend, a.backup_dir, a.lba, a.expect_current_sha256, a.execute, log))
    except (PatchError, OSError, KeyError, ValueError) as e:
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
