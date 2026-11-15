#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read-only fetch of named files from the J313 internal SSD (NTFS Windows volume and FAT ESP).

Companion of ans_offline_recon.py: same block devices (proxy, image, saved blocks), same NTFS
reader.  It never writes to the disk.  Every block read is saved (--save-blocks) and, when
--compare-blocks names the directory of an earlier run, compared with the earlier copy; blocks
that differ are listed (they mean the disk changed between the two runs).

Each item is independent: an item that cannot be read is reported with its error and the others
are still read.  A device read error aborts the run.

Run with `python3 -I`.  Standard library only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ans_offline_recon as R  # noqa: E402

# (path, first byte, byte count or None for the whole file)
NTFS_FILES = [
    ("\\Windows\\System32\\config\\SYSTEM", 0, None),
    ("\\Windows\\System32\\config\\SYSTEM.LOG1", 0, None),
    ("\\Windows\\System32\\config\\SYSTEM.LOG2", 0, None),
    ("\\pagefile.sys", 0, 65536),
    ("\\DumpStack.log.tmp", 0, None),
    ("\\DumpStack.log", 0, None),
    ("\\Windows\\bootstat.dat", 0, None),
    ("\\Windows\\System32\\Recovery\\ReAgent.xml", 0, None),
    ("\\Windows\\System32\\LogFiles\\Srt\\SrtTrail.txt", 0, None),
    ("\\Windows\\System32\\LogFiles\\Srt\\SrtTrail.log", 0, None),
    ("\\Windows\\MEMORY.DMP", 0, 8192),
]
NTFS_DIRS = [
    "\\", "\\Windows\\System32\\config", "\\Windows\\System32\\LogFiles\\Srt", "\\Windows\\Minidump",
    "\\Recovery", "\\Recovery\\WindowsRE", "\\Windows\\System32\\Recovery", "\\Windows\\LiveKernelReports",
]
FAT_FILES = [
    "\\EFI\\Microsoft\\Boot\\BCD", "\\EFI\\Microsoft\\Boot\\BCD.LOG", "\\EFI\\Microsoft\\Boot\\BCD.LOG1",
    "\\EFI\\Microsoft\\Boot\\BCD.LOG2", "\\EFI\\Microsoft\\Boot\\bootstat.dat",
    "\\EFI\\Microsoft\\Recovery\\BCD", "\\EFI\\Microsoft\\Recovery\\BCD.LOG",
    "\\EFI\\Microsoft\\Recovery\\BCD.LOG1", "\\EFI\\Microsoft\\Recovery\\BCD.LOG2",
]
FAT_DIRS = ["\\", "\\EFI", "\\EFI\\Microsoft", "\\EFI\\Microsoft\\Boot", "\\EFI\\Microsoft\\Recovery", "\\EFI\\BOOT"]
ESP_TYPE = "c12a7328-f81f-11d2-ba4b-00a0c93ec93b"
MAX_WHOLE = 64 << 20


def sha256(b):
    return hashlib.sha256(b).hexdigest()


class Fat:
    """Read-only FAT12/16/32 over a Disk (sector size from the boot sector)."""

    def __init__(self, disk, first_lba):
        self.disk, self.base = disk, first_lba * disk.block
        b = disk.read(self.base, 512)
        if b[510:512] != b"\x55\xaa":
            raise R.ReconError("no FAT boot signature")
        self.bps, self.spc, self.reserved, self.nfats, self.root_entries, tot16 = struct.unpack_from("<HBHBHH", b, 11)
        fatsz16 = struct.unpack_from("<H", b, 22)[0]
        tot32, fatsz32 = struct.unpack_from("<II", b, 32)
        if self.bps not in (512, 1024, 2048, 4096) or not self.spc or not self.nfats:
            raise R.ReconError("implausible FAT boot sector")
        self.fatsz = fatsz16 or fatsz32
        total = tot16 or tot32
        self.root_sectors = (self.root_entries * 32 + self.bps - 1) // self.bps
        self.first_data = self.reserved + self.nfats * self.fatsz + self.root_sectors
        self.clusters = (total - self.first_data) // self.spc
        self.kind = 12 if self.clusters < 4085 else 16 if self.clusters < 65525 else 32
        self.root_cluster = struct.unpack_from("<I", b, 44)[0] if self.kind == 32 else 0
        self.csize = self.bps * self.spc

    def _fat(self, n):
        off = self.base + self.reserved * self.bps
        if self.kind == 32:
            return struct.unpack("<I", self.disk.read(off + 4 * n, 4))[0] & 0x0fffffff
        if self.kind == 16:
            return struct.unpack("<H", self.disk.read(off + 2 * n, 2))[0]
        v = struct.unpack("<H", self.disk.read(off + n + n // 2, 2))[0]
        return v >> 4 if n & 1 else v & 0xfff

    def chain(self, first):
        end = {12: 0xff8, 16: 0xfff8, 32: 0x0ffffff8}[self.kind]
        out, n = [], first
        while 2 <= n < end:
            if n >= self.clusters + 2 or len(out) > self.clusters:
                raise R.ReconError("FAT chain leaves the volume or loops")
            out.append(n)
            n = self._fat(n)
        return out

    def cluster_offset(self, n):
        return self.base + (self.first_data + (n - 2) * self.spc) * self.bps

    def _dir_bytes(self, first):
        if first == 0 and self.kind != 32:
            return self.disk.read(self.base + (self.reserved + self.nfats * self.fatsz) * self.bps,
                                  self.root_sectors * self.bps)
        return b"".join(self.disk.read(self.cluster_offset(c), self.csize)
                        for c in self.chain(first or self.root_cluster))

    def listdir(self, first):
        raw, out, lfn = self._dir_bytes(first), [], []
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xe5:
                lfn = []
                continue
            if e[11] == 0x0f:
                lfn.insert(0, e[1:11] + e[14:26] + e[28:32])
                continue
            short = e[:8].decode("latin-1").rstrip() + ("." + e[8:11].decode("latin-1").rstrip() if e[8:11].strip() else "")
            name = b"".join(lfn).decode("utf-16-le", "replace").split("\0")[0] if lfn else short
            lfn = []
            if e[11] & 0x08:
                continue
            hi, wtime, wdate, lo, size = struct.unpack_from("<HHHHI", e, 20)
            out.append({"name": name, "short": short, "dir": bool(e[11] & 0x10), "size": size,
                        "cluster": hi << 16 | lo,
                        "written": f"{1980 + (wdate >> 9):04d}-{wdate >> 5 & 15:02d}-{wdate & 31:02d} "
                                   f"{wtime >> 11:02d}:{wtime >> 5 & 63:02d}:{(wtime & 31) * 2:02d} (local)"})
        return out

    def find(self, path):
        cur = {"dir": True, "cluster": 0, "size": 0, "name": "\\"}
        for part in [p for p in path.replace("/", "\\").split("\\") if p]:
            if not cur["dir"]:
                return None
            hit = [e for e in self.listdir(cur["cluster"])
                   if part.upper() in (e["name"].upper(), e["short"].upper())]
            if not hit:
                return None
            cur = hit[0]
        return cur

    def read_file(self, entry):
        data, lbas = bytearray(), []
        for c in self.chain(entry["cluster"]):
            off = self.cluster_offset(c)
            data += self.disk.read(off, self.csize)
            lbas += list(range(off // self.disk.block, (off + self.csize - 1) // self.disk.block + 1))
        if len(data) < entry["size"]:
            raise R.ReconError("FAT chain shorter than the file size")
        return bytes(data[:entry["size"]]), sorted(set(lbas))


def clean(e):
    return {k: (v if not isinstance(v, bytes) else v.hex()) for k, v in e.items()}


def fetch_ntfs(disk, part, out_dir):
    vol = R.Ntfs(disk, part["first_lba"])
    items, lists = [], []
    for path, start, count in NTFS_FILES:
        item = {"path": path}
        items.append(item)
        try:
            rec = vol.resolve(path)
            if rec is None:
                item["found"] = False
                continue
            item.update(found=True, mft_record=rec)
            st = vol.stream(rec, 0x80, "")
            if st is None:
                item["error"] = "no unnamed data stream"
                continue
            item["size"] = st.size
            item["resident"] = st.resident is not None
            n = st.size - start if count is None else min(count, st.size - start)
            if n <= 0:
                item["read"] = 0
                continue
            if n > MAX_WHOLE:
                item["error"] = f"{n} bytes is over the {MAX_WHOLE} byte limit"
                continue
            if st.resident is None:
                item["runs"] = [{"lcn": l, "clusters": c} for l, c in st.runs[:64]]
                item["run_count"] = len(st.runs)
                locs = R.lbas_of(vol, st, start, n)
                item["first_lba"], item["last_lba"] = locs[0]["lba"], locs[-1]["lba"]
                item["blocks"] = len({x["lba"] for x in locs})
            data = st.read(start, n)
            item.update(read=n, offset=start, sha256=sha256(data))
            if out_dir:
                name = "ntfs" + path.replace("\\", "_")
                (out_dir / name).write_bytes(data)
                item["saved_as"] = name
        except R.ReconError as e:
            item["error"] = str(e)
    for path in NTFS_DIRS:
        d = {"path": path}
        lists.append(d)
        try:
            rec = vol.resolve(path)
            if rec is None:
                d["found"] = False
                continue
            d["found"] = True
            d["entries"] = sorted((clean(e) for e in vol.listdir(rec) if e.get("namespace") != 2),
                                  key=lambda e: e["name"].upper())
        except R.ReconError as e:
            d["error"] = str(e)
    return {"partition": part, "files": items, "directories": lists}


def fetch_fat(disk, part, out_dir):
    out = {"partition": part, "files": [], "directories": []}
    try:
        fat = Fat(disk, part["first_lba"])
    except R.ReconError as e:
        out["error"] = str(e)
        return out
    out["fat"] = {"kind": fat.kind, "bytes_per_sector": fat.bps, "sectors_per_cluster": fat.spc, "clusters": fat.clusters}
    for path in FAT_FILES:
        item = {"path": path}
        out["files"].append(item)
        try:
            e = fat.find(path)
            if e is None or e["dir"]:
                item["found"] = False
                continue
            item.update(found=True, size=e["size"], written=e["written"])
            if e["size"] > MAX_WHOLE:
                item["error"] = "over the size limit"
                continue
            data, lbas = fat.read_file(e) if e["size"] else (b"", [])
            item.update(read=len(data), sha256=sha256(data), lbas=lbas)
            if out_dir:
                name = f"esp{part['index']}" + path.replace("\\", "_")
                (out_dir / name).write_bytes(data)
                item["saved_as"] = name
        except R.ReconError as err:
            item["error"] = str(err)
    for path in FAT_DIRS:
        d = {"path": path}
        out["directories"].append(d)
        try:
            e = fat.find(path)
            if e is None or not e["dir"]:
                d["found"] = False
                continue
            d["found"] = True
            d["entries"] = [x for x in fat.listdir(e["cluster"]) if x["name"] not in (".", "..")]
        except R.ReconError as err:
            d["error"] = str(err)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description="Read-only fetch of boot-state files from the internal SSD")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--image", type=Path)
    src.add_argument("--proxyclient", type=Path)
    src.add_argument("--blocks", type=Path)
    ap.add_argument("--output", type=Path, required=True, help="JSON receipt (must not exist)")
    ap.add_argument("--save-blocks", type=Path, help="directory that receives every block read")
    ap.add_argument("--save-files", type=Path, help="directory that receives the files read")
    ap.add_argument("--compare-blocks", type=Path, help="blocks of an earlier run: report blocks that differ")
    ap.add_argument("--windows-partition", type=int, default=6, help="GPT entry index of the Windows volume")
    a = ap.parse_args(argv)
    if a.output.exists():
        ap.error("refusing to overwrite an existing receipt")
    if a.save_files:
        a.save_files.mkdir(parents=True, exist_ok=True)
    dev = R.ImageDevice(a.image) if a.image else R.BlocksDevice(a.blocks) if a.blocks else R.ProxyDevice(a.proxyclient)
    receipt = {"tool": "ans_offline_fetch", "version": 1, "read_only": True}
    try:
        disk = R.Disk(dev, a.save_blocks)
        try:
            gpt = R.parse_gpt(disk)
            receipt["gpt_ok"] = gpt["header_crc_ok"] and gpt["entries_crc_ok"]
            win = [p for p in gpt["partitions"] if p["index"] == a.windows_partition]
            if not win:
                raise R.ReconError("no such Windows partition index")
            receipt["windows"] = fetch_ntfs(disk, win[0], a.save_files)
            receipt["esp"] = [fetch_fat(disk, p, a.save_files) for p in gpt["partitions"] if p["type"] == ESP_TYPE]
        except R.ReconError as e:
            receipt["error"] = str(e)
        receipt["blocks_read"] = len(disk.cache)
        if a.compare_blocks:
            same, differ = 0, []
            for lba, data in sorted(disk.cache.items()):
                p = a.compare_blocks / f"lba-{lba:012d}.bin"
                if p.is_file():
                    if p.read_bytes() == data:
                        same += 1
                    else:
                        differ.append(lba)
            receipt["compare"] = {"directory": str(a.compare_blocks), "common_blocks": same + len(differ),
                                  "identical": same, "different_lbas": differ}
    finally:
        dev.close()
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(receipt, indent=1))
    c = receipt.get("compare")
    tail = f" compared={c['common_blocks']} different={len(c['different_lbas'])}" if c else ""
    errs = sum(1 for f in receipt.get("windows", {}).get("files", []) if "error" in f)
    print(("FETCH_ERROR " + receipt["error"]) if "error" in receipt else
          f"FETCH_OK blocks_read={receipt['blocks_read']} item_errors={errs}{tail} -> {a.output}")
    return 3 if "error" in receipt else 0


if __name__ == "__main__":
    sys.exit(main())
