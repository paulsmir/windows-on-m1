#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""READ-ONLY reconnaissance of the Windows boot volume on the J313 internal SSD.

Reads the disk either from an image file (--image) or through the m1n1 proxy (--proxyclient:
nvme_init, nvme_read of namespace 1, nvme_shutdown; no other proxy operation, no write, no
flush, no PCIe call).  It finds the NTFS volume that holds the SYSTEM hive, resolves the hive
through the MFT, and reports for the current control set
  Services\\stornvme\\StartOverride value "0"   and   Services\\AppleAns\\ImagePath
with the exact disk location (LBA and byte offset) of the bytes that hold them, the state of
the hive (base block, sequence numbers, checksum, transaction logs) and the AppleAns.sys files
in the driver store.  Output: one JSON receipt.  Every block read can be saved (--save-blocks).

Run with `python3 -I`.  Standard library only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

BLOCK = 4096  # ANS namespace 1 logical block size


class ReconError(Exception):
    pass


# ---- block devices (read only) ------------------------------------------------------------
class ImageDevice:
    def __init__(self, path, block=BLOCK):
        self.f = open(path, "rb")
        self.block = block

    def read_block(self, lba):
        self.f.seek(lba * self.block)
        data = self.f.read(self.block)
        if len(data) != self.block:
            raise ReconError(f"image read past end at LBA {lba}")
        return data

    def close(self):
        self.f.close()


class ProxyDevice:
    """m1n1 proxy: P_NVME_INIT once, P_NVME_READ per block, P_NVME_SHUTDOWN at the end."""

    def __init__(self, proxyclient):
        sys.path.insert(0, str(Path(proxyclient).resolve()))
        from m1n1.proxy import UartInterface, M1N1Proxy  # noqa: E402
        from m1n1.proxyutils import ProxyUtils, bootstrap_port  # noqa: E402
        self.block = BLOCK
        self.iface = UartInterface()
        self.p = M1N1Proxy(self.iface, debug=False)
        bootstrap_port(self.iface, self.p)
        self.u = ProxyUtils(self.p)
        self.bfr = self.u.memalign(0x4000, BLOCK)
        self.up = False
        rc = self.p.nvme_init()
        if rc != 1:
            self.u.free(self.bfr)
            raise ReconError(f"nvme_init returned {rc}")
        self.up = True

    def read_block(self, lba):
        rc = self.p.nvme_read(1, lba, self.bfr)
        if rc != 1:
            raise ReconError(f"nvme_read LBA {lba} returned {rc}")
        return bytes(self.iface.readmem(self.bfr, BLOCK))

    def close(self):
        try:
            if self.up:
                self.p.nvme_shutdown()
                self.up = False
        finally:
            self.u.free(self.bfr)


class Disk:
    """Caching byte reader over a block device; optionally keeps a copy of every block read."""

    def __init__(self, dev, save_dir=None):
        self.dev = dev
        self.block = dev.block
        self.cache = {}
        self.save_dir = Path(save_dir) if save_dir else None
        if self.save_dir:
            self.save_dir.mkdir(parents=True, exist_ok=True)

    def read_block(self, lba):
        data = self.cache.get(lba)
        if data is None:
            data = self.dev.read_block(lba)
            self.cache[lba] = data
            if self.save_dir:
                (self.save_dir / f"lba-{lba:012d}.bin").write_bytes(data)
        return data

    def read(self, offset, length):
        out = bytearray()
        while length:
            lba, off = divmod(offset, self.block)
            n = min(length, self.block - off)
            out += self.read_block(lba)[off:off + n]
            offset += n
            length -= n
        return bytes(out)


# ---- GPT -----------------------------------------------------------------------------------
def guid_text(b):
    d1, d2, d3 = struct.unpack_from("<IHH", b, 0)
    return f"{d1:08x}-{d2:04x}-{d3:04x}-{b[8:10].hex()}-{b[10:16].hex()}"


def parse_gpt(disk):
    h = disk.read_block(1)
    if h[:8] != b"EFI PART":
        raise ReconError("no GPT header at LBA 1")
    hsize, hcrc = struct.unpack_from("<II", h, 12)
    ent_lba, count, esize, ecrc = struct.unpack_from("<QIII", h, 72)
    if not 92 <= hsize <= disk.block or esize < 128 or count > 4096:
        raise ReconError("implausible GPT header")
    header_ok = zlib.crc32(h[:16] + b"\0\0\0\0" + h[20:hsize]) == hcrc
    raw = disk.read(ent_lba * disk.block, count * esize)
    parts = []
    for i in range(count):
        e = raw[i * esize:(i + 1) * esize]
        if e[:16] == bytes(16):
            continue
        first, last = struct.unpack_from("<QQ", e, 32)
        parts.append({"index": i, "type": guid_text(e[:16]), "guid": guid_text(e[16:32]),
                      "first_lba": first, "last_lba": last,
                      "name": e[56:128].decode("utf-16-le", "replace").split("\0")[0]})
    return {"header_crc_ok": header_ok, "entries_crc_ok": zlib.crc32(raw) == ecrc,
            "disk_guid": guid_text(h[56:72]), "partitions": parts}


# ---- NTFS ----------------------------------------------------------------------------------
def fixup(buf, what):
    """Apply the update sequence array of a FILE/INDX record; returns the usable bytes."""
    b = bytearray(buf)
    usa_off, usa_count = struct.unpack_from("<HH", b, 4)
    if usa_count < 2 or usa_off + 2 * usa_count > len(b):
        raise ReconError(f"{what}: bad update sequence array")
    stride = len(b) // (usa_count - 1)
    if stride * (usa_count - 1) != len(b) or stride < 256:
        raise ReconError(f"{what}: update sequence does not cover the record")
    usn = b[usa_off:usa_off + 2]
    for i in range(1, usa_count):
        end = i * stride
        if b[end - 2:end] != usn:
            raise ReconError(f"{what}: torn record (update sequence mismatch in part {i})")
        b[end - 2:end] = b[usa_off + 2 * i:usa_off + 2 * i + 2]
    return bytes(b)


def decode_runs(data):
    """Data runs -> [(lcn or None for sparse, clusters)]."""
    runs, pos, lcn = [], 0, 0
    while pos < len(data) and data[pos]:
        head = data[pos]
        nlen, noff = head & 15, head >> 4
        pos += 1
        if not nlen or nlen > 8 or noff > 8 or pos + nlen + noff > len(data):
            raise ReconError("bad data run")
        count = int.from_bytes(data[pos:pos + nlen], "little")
        pos += nlen
        if noff:
            lcn += int.from_bytes(data[pos:pos + noff], "little", signed=True)
            runs.append((lcn, count))
        else:
            runs.append((None, count))
        pos += noff
    return runs


class Stream:
    """One attribute's content: resident bytes or a list of extents on the volume."""

    def __init__(self, vol, resident=None, runs=None, size=0, flags=0):
        self.vol, self.resident, self.runs, self.size, self.flags = vol, resident, runs or [], size, flags

    def locate(self, offset, length):
        """[(volume byte offset, length)] of a byte range of a non-resident stream."""
        if self.resident is not None:
            raise ReconError("resident data has no extent")
        if self.flags & 0x4001:
            raise ReconError("compressed or encrypted data is not supported")
        if offset < 0 or offset + length > self.size:
            raise ReconError("range outside the stream")
        out, cs, vbase = [], self.vol.cluster, 0
        for lcn, count in self.runs:
            vend = vbase + count * cs
            while length and vbase <= offset < vend:
                if lcn is None:
                    raise ReconError("range lies in a sparse run")
                n = min(length, vend - offset)
                out.append((lcn * cs + (offset - vbase), n))
                offset += n
                length -= n
            vbase = vend
        if length:
            raise ReconError("range not covered by the run list")
        return out

    def read(self, offset, length):
        if self.resident is not None:
            if offset + length > len(self.resident):
                raise ReconError("range outside resident data")
            return self.resident[offset:offset + length]
        return b"".join(self.vol.read(o, n) for o, n in self.locate(offset, length))


class Ntfs:
    def __init__(self, disk, first_lba):
        self.disk, self.base = disk, first_lba * disk.block
        b = disk.read(self.base, 512)
        if b[3:11] != b"NTFS    ":
            raise ReconError("not an NTFS boot sector")
        self.sector = struct.unpack_from("<H", b, 0x0b)[0]
        spc = b[0x0d]
        self.cluster = self.sector * (spc if spc < 0x80 else 1 << (256 - spc))
        self.total_sectors, self.mft_lcn = struct.unpack_from("<QQ", b, 0x28)
        self.serial = struct.unpack_from("<Q", b, 0x48)[0]

        def size_field(v):
            v = v - 256 if v > 127 else v
            return (1 << -v) if v < 0 else v * self.cluster
        self.record_size = size_field(b[0x40])
        self.index_size = size_field(b[0x44])
        if self.sector not in (512, 1024, 2048, 4096) or self.cluster % self.sector or \
                self.record_size not in (1024, 2048, 4096) or self.cluster > 2 << 20:
            raise ReconError("implausible NTFS geometry")
        self.records = {}
        # $MFT bootstrap: its first record lies at mft_lcn; later extents come from its run list.
        first = self._parse_record(self.read(self.mft_lcn * self.cluster, self.record_size), 0)
        own = [a for a in first["attrs"] if a["type"] == 0x80 and a["name"] == "" and a.get("start_vcn") == 0]
        if not own:
            raise ReconError("$MFT record has no first data extent")
        self.mft = Stream(self, runs=own[0]["runs"], size=own[0]["size"])
        self.records[0] = first
        self.mft = self.stream(0, 0x80, "")

    def read(self, offset, length):
        return self.disk.read(self.base + offset, length)

    def lba_of(self, volume_offset):
        return divmod(self.base + volume_offset, self.disk.block)

    def _parse_record(self, raw, number):
        if raw[:4] != b"FILE":
            raise ReconError(f"MFT record {number}: signature {raw[:4]!r}")
        b = fixup(raw, f"MFT record {number}")
        seq, links, attr_off, flags, used = struct.unpack_from("<HHHHI", b, 16)
        base_ref = struct.unpack_from("<Q", b, 32)[0]
        attrs, pos = [], attr_off
        while pos + 8 <= used and pos + 8 <= len(b):
            atype, alen = struct.unpack_from("<II", b, pos)
            if atype == 0xffffffff:
                break
            if alen < 24 or pos + alen > len(b):
                raise ReconError(f"MFT record {number}: bad attribute length")
            nonres, nlen, noff, aflags = b[pos + 8], b[pos + 9], *struct.unpack_from("<HH", b, pos + 10)
            name = b[pos + noff:pos + noff + 2 * nlen].decode("utf-16-le")
            a = {"type": atype, "name": name, "flags": aflags, "record": number}
            if nonres:
                svcn, evcn, roff = struct.unpack_from("<QQH", b, pos + 16)
                a.update(start_vcn=svcn, runs=decode_runs(b[pos + roff:pos + alen]))
                if svcn == 0:
                    a.update(allocated=struct.unpack_from("<Q", b, pos + 40)[0],
                             size=struct.unpack_from("<Q", b, pos + 48)[0],
                             initialized=struct.unpack_from("<Q", b, pos + 56)[0])
            else:
                clen, coff = struct.unpack_from("<IH", b, pos + 16)
                if coff + clen > alen:
                    raise ReconError(f"MFT record {number}: resident content outside attribute")
                a["content"] = b[pos + coff:pos + coff + clen]
            attrs.append(a)
            pos += alen
        return {"number": number, "sequence": seq, "links": links, "flags": flags,
                "in_use": bool(flags & 1), "directory": bool(flags & 2),
                "base": base_ref & 0xffffffffffff, "attrs": attrs}

    def record(self, number):
        r = self.records.get(number)
        if r is None:
            raw = self.mft.read(number * self.record_size, self.record_size)
            r = self.records[number] = self._parse_record(raw, number)
        return r

    def attributes(self, number):
        """All attributes of a file, following $ATTRIBUTE_LIST into its extension records."""
        base = self.record(number)
        if not base["in_use"]:
            raise ReconError(f"MFT record {number} is not in use")
        attrs = list(base["attrs"])
        for a in base["attrs"]:
            if a["type"] != 0x20:
                continue
            if "content" in a:
                data = a["content"]
            else:
                data = Stream(self, runs=a["runs"], size=a["size"], flags=a["flags"]).read(0, a["size"])
            pos, seen = 0, {number}
            while pos + 26 <= len(data):
                elen = struct.unpack_from("<H", data, pos + 4)[0]
                if elen < 26:
                    raise ReconError("bad attribute list entry")
                ref = struct.unpack_from("<Q", data, pos + 16)[0] & 0xffffffffffff
                if ref not in seen:
                    seen.add(ref)
                    ext = self.record(ref)
                    if ext["base"] != number:
                        raise ReconError(f"record {ref} is not an extension of {number}")
                    attrs += ext["attrs"]
                pos += elen
        return attrs

    def stream(self, number, atype, name):
        parts = [a for a in self.attributes(number) if a["type"] == atype and a["name"] == name]
        if not parts:
            return None
        if "content" in parts[0]:
            return Stream(self, resident=parts[0]["content"], size=len(parts[0]["content"]), flags=parts[0]["flags"])
        parts.sort(key=lambda a: a["start_vcn"])
        if parts[0]["start_vcn"] != 0:
            raise ReconError("attribute without its first extent")
        runs, vcn = [], 0
        for a in parts:
            if a["start_vcn"] != vcn:
                raise ReconError("attribute extents are not contiguous in VCN")
            runs += a["runs"]
            vcn += sum(c for _, c in a["runs"])
        return Stream(self, runs=runs, size=parts[0]["size"], flags=parts[0]["flags"])

    # -- directories
    @staticmethod
    def _key(name):
        return [ord(c.upper()) if len(c.upper()) == 1 else ord(c) for c in name]

    @staticmethod
    def _entries(buf, start):
        pos, out = start, []
        while pos + 16 <= len(buf):
            ref, elen, klen, flags = struct.unpack_from("<QHHI", buf, pos)
            if elen < 16 or pos + elen > len(buf):
                raise ReconError("bad index entry")
            e = {"flags": flags, "ref": ref & 0xffffffffffff}
            if flags & 1:
                e["child"] = struct.unpack_from("<Q", buf, pos + elen - 8)[0]
            if not flags & 2 and klen >= 66:
                nlen = buf[pos + 16 + 64]
                e["name"] = buf[pos + 16 + 66:pos + 16 + 66 + 2 * nlen].decode("utf-16-le")
                e["namespace"] = buf[pos + 16 + 65]
                e["size"] = struct.unpack_from("<Q", buf, pos + 16 + 48)[0]
                e["dir"] = bool(struct.unpack_from("<I", buf, pos + 16 + 56)[0] & 0x10000000)
            out.append(e)
            if flags & 2:
                break
            pos += elen
        return out

    def _index(self, number):
        root = self.stream(number, 0x90, "$I30")
        if root is None:
            raise ReconError(f"MFT record {number} is not a directory")
        r = root.read(0, root.size)
        block_size = struct.unpack_from("<I", r, 8)[0]
        eoff, total = struct.unpack_from("<II", r, 16)
        root_entries = self._entries(r[:16 + total], 16 + eoff)
        alloc = self.stream(number, 0xa0, "$I30")
        unit = self.cluster if self.cluster <= block_size else 512

        def node(vcn):
            raw = alloc.read(vcn * unit, block_size)
            if raw[:4] != b"INDX":
                raise ReconError("index block without INDX signature")
            b = fixup(raw, "index block")
            eo, tot = struct.unpack_from("<II", b, 24)
            return self._entries(b[:24 + tot], 24 + eo)
        return root_entries, node, alloc, unit, block_size

    def lookup(self, number, name):
        """B+tree descent by name; returns the index entry or None."""
        entries, node, _, _, _ = self._index(number)
        want = self._key(name)
        for _ in range(16):
            down = None
            for e in entries:
                if "name" in e:
                    k = self._key(e["name"])
                    if k == want:
                        return e
                    if want < k:
                        down = e.get("child", -1)
                        break
                else:
                    down = e.get("child", -1)
            if down is None or down < 0:
                return None
            entries = node(down)
        raise ReconError("index deeper than 16 levels")

    def listdir(self, number):
        """Every entry of a directory (all allocated index blocks)."""
        entries, node, alloc, unit, block_size = self._index(number)
        out = [e for e in entries if "name" in e]
        if alloc is not None:
            bitmap = self.stream(number, 0xb0, "$I30")
            bits = bitmap.read(0, bitmap.size) if bitmap else b""
            for i in range(alloc.size // block_size):
                if i // 8 < len(bits) and bits[i // 8] >> (i % 8) & 1:
                    out += [e for e in node(i * block_size // unit) if "name" in e]
        return out

    def resolve(self, path):
        number, e = 5, None
        for part in [p for p in path.replace("/", "\\").split("\\") if p]:
            e = self.lookup(number, part)
            if e is None:
                return None
            number = e["ref"]
        return number


def lbas_of(vol, stream, offset, length):
    """Disk location of a byte range of a file: [{lba, offset, length}], split at LBA edges."""
    out = []
    for voff, n in stream.locate(offset, length):
        while n:
            lba, off = vol.lba_of(voff)
            take = min(n, vol.disk.block - off)
            out.append({"lba": lba, "offset": off, "length": take})
            voff += take
            n -= take
    return out


# ---- registry hive -------------------------------------------------------------------------
def regf_checksum(base):
    x = 0
    for (v,) in struct.iter_unpack("<I", base[:508]):
        x ^= v
    return 0xfffffffe if x == 0xffffffff else 1 if x == 0 else x


def parse_base_block(b):
    if b[:4] != b"regf":
        raise ReconError("no regf signature")
    seq1, seq2 = struct.unpack_from("<II", b, 4)
    major, minor, ftype, fmt, root, bins, cluster = struct.unpack_from("<7I", b, 20)
    stored = struct.unpack_from("<I", b, 508)[0]
    return {"primary_sequence": seq1, "secondary_sequence": seq2, "clean": seq1 == seq2,
            "timestamp": struct.unpack_from("<Q", b, 12)[0], "version": f"{major}.{minor}",
            "file_type": ftype, "format": fmt, "root_cell": root, "hive_bins_size": bins,
            "clustering": cluster, "checksum_stored": stored, "checksum_computed": regf_checksum(b),
            "checksum_ok": stored == regf_checksum(b),
            "file_name": b[48:112].decode("utf-16-le", "replace").split("\0")[0]}


class Hive:
    """Read-only regf parser over read(offset, length) of the primary file."""

    def __init__(self, read, size):
        self.read, self.size = read, size
        self.base = parse_base_block(read(0, 4096))
        if self.base["hive_bins_size"] + 4096 > size:
            raise ReconError("hive bins extend past the file")

    def verify_bins(self):
        """Walk every bin header: signature, own offset and size.  Proves the file's run list
        maps the whole hive consistently (one block read per bin)."""
        pos, count, total = 0, 0, self.base["hive_bins_size"]
        while pos < total:
            h = self.read(4096 + pos, 12)
            off, size = struct.unpack_from("<II", h, 4)
            if h[:4] != b"hbin" or off != pos or not size or size % 4096 or pos + size > total:
                raise ReconError(f"bad hive bin at {pos:#x}")
            pos += size
            count += 1
        return count

    def cell(self, offset):
        """(file offset of the cell's size field, cell data) for a cell offset."""
        if offset in (0, 0xffffffff) or offset & 7 or offset + 4 > self.base["hive_bins_size"]:
            raise ReconError(f"bad cell offset {offset:#x}")
        pos = 4096 + offset
        size = struct.unpack("<i", self.read(pos, 4))[0]
        if size >= 0 or -size < 8 or -size > 1 << 20 or offset - size > self.base["hive_bins_size"]:
            raise ReconError(f"cell {offset:#x} is free or has a bad size ({size})")
        return pos, self.read(pos + 4, -size - 4)

    def nk(self, offset):
        pos, d = self.cell(offset)
        if d[:2] != b"nk":
            raise ReconError(f"cell {offset:#x} is not a key node")
        flags = struct.unpack_from("<H", d, 2)[0]
        nsub, _, sublist, _, nval, vallist = struct.unpack_from("<6I", d, 20)
        nlen = struct.unpack_from("<H", d, 72)[0]
        raw = d[76:76 + nlen]
        return {"offset": offset, "file_offset": pos, "subkeys": nsub, "subkey_list": sublist,
                "values": nval, "value_list": vallist,
                "name": raw.decode("latin-1") if flags & 0x20 else raw.decode("utf-16-le")}

    def _subkey_offsets(self, list_offset, depth=0):
        if depth > 4:
            raise ReconError("subkey list nested too deeply")
        _, d = self.cell(list_offset)
        sig, count = d[:2], struct.unpack_from("<H", d, 2)[0]
        if sig in (b"lf", b"lh"):
            return [struct.unpack_from("<I", d, 4 + 8 * i)[0] for i in range(count)]
        if sig == b"li":
            return [struct.unpack_from("<I", d, 4 + 4 * i)[0] for i in range(count)]
        if sig == b"ri":
            out = []
            for i in range(count):
                out += self._subkey_offsets(struct.unpack_from("<I", d, 4 + 4 * i)[0], depth + 1)
            return out
        raise ReconError(f"unknown subkey list {sig!r}")

    def subkeys(self, key):
        if not key["subkeys"]:
            return []
        return [self.nk(o) for o in self._subkey_offsets(key["subkey_list"])]

    def subkey(self, key, name):
        for k in self.subkeys(key):
            if k["name"].lower() == name.lower():
                return k
        return None

    def open(self, path):
        key = self.nk(self.base["root_cell"])
        for part in [p for p in path.split("\\") if p]:
            key = self.subkey(key, part)
            if key is None:
                return None
        return key

    def values(self, key):
        if not key["values"]:
            return []
        _, lst = self.cell(key["value_list"])
        out = []
        for i in range(key["values"]):
            voff = struct.unpack_from("<I", lst, 4 * i)[0]
            pos, d = self.cell(voff)
            if d[:2] != b"vk":
                raise ReconError(f"cell {voff:#x} is not a value")
            nlen, dsize, doff, vtype, flags = struct.unpack_from("<HIIIH", d, 2)
            raw = d[20:20 + nlen]
            v = {"name": raw.decode("latin-1") if flags & 1 else raw.decode("utf-16-le"),
                 "type": vtype, "vk_cell": voff, "vk_file_offset": pos,
                 "inline": bool(dsize & 0x80000000), "data_size": dsize & 0x7fffffff}
            if v["inline"]:
                if v["data_size"] > 4:
                    raise ReconError("inline value larger than four bytes")
                v["data_file_offset"] = pos + 4 + 8  # the vk data-offset field holds the data
                v["data"] = d[8:8 + v["data_size"]]
            elif v["data_size"] > 16344:
                v["data_file_offset"] = None
                v["data"] = None  # big-data value: not needed here
            else:
                dpos, dd = self.cell(doff)
                if v["data_size"] > len(dd):
                    raise ReconError("value data larger than its cell")
                v["data_cell"] = doff
                v["data_file_offset"] = dpos + 4
                v["data"] = dd[:v["data_size"]]
            out.append(v)
        return out

    def value(self, key, name):
        for v in self.values(key):
            if v["name"].lower() == name.lower():
                return v
        return None


def value_text(v):
    if v is None or v["data"] is None:
        return None
    if v["type"] in (1, 2):
        return v["data"].decode("utf-16-le", "replace").split("\0")[0]
    if v["type"] == 4 and len(v["data"]) == 4:
        return struct.unpack("<I", v["data"])[0]
    return v["data"].hex()


def parse_log(read, size, targets):
    """Transaction log (.LOG1/.LOG2): base block and the log entries (new format)."""
    if size < 1024:
        return {"size": size, "note": "empty"}
    try:
        out = {"size": size, "base": parse_base_block(read(0, 512))}
    except ReconError as e:
        return {"size": size, "error": str(e)}
    entries, pos = [], 512
    while pos + 40 <= size and len(entries) < 4096:
        h = read(pos, 40)
        if h[:4] != b"HvLE":
            break
        esize, flags, seq, bins, pages = struct.unpack_from("<5I", h, 4)
        if esize < 40 or esize % 512 or pos + esize > size or pages > (esize - 40) // 8:
            break
        refs = read(pos + 40, 8 * pages)
        hits = []
        for i in range(pages):
            off, length = struct.unpack_from("<II", refs, 8 * i)
            for name, (toff, tlen) in targets.items():
                if off < toff - 4096 + tlen and toff - 4096 < off + length:
                    hits.append(name)
        entries.append({"file_offset": pos, "size": esize, "flags": flags, "sequence": seq,
                        "hive_bins_size": bins, "dirty_pages": pages, "touches": sorted(set(hits))})
        pos += esize
    out["entries"] = entries
    out["old_format_dirty_vector"] = read(512, 4) == b"DIRT"
    return out


# ---- the reconnaissance --------------------------------------------------------------------
def sha256(b):
    return hashlib.sha256(b).hexdigest()


def describe_value(vol, stream, v):
    if v is None:
        return None
    d = {k: v[k] for k in ("name", "type", "inline", "data_size", "vk_cell", "vk_file_offset")}
    d["value"] = value_text(v)
    d["data_hex"] = v["data"].hex() if v["data"] is not None else None
    d["data_file_offset"] = v["data_file_offset"]
    d["vk_location"] = lbas_of(vol, stream, v["vk_file_offset"], 24)
    if v["data_file_offset"] is not None and v["data_size"]:
        d["data_location"] = lbas_of(vol, stream, v["data_file_offset"], v["data_size"])
        for part in d["data_location"]:
            part["lba_sha256"] = sha256(vol.disk.read_block(part["lba"]))
    return d


def recon_volume(disk, part, hive_path, store_path, old_store):
    vol = Ntfs(disk, part["first_lba"])
    out = {"partition": part, "ntfs": {"sector": vol.sector, "cluster": vol.cluster,
           "record_size": vol.record_size, "index_block": vol.index_size,
           "serial": f"{vol.serial:016x}", "mft_lcn": vol.mft_lcn}}
    rec = vol.resolve(hive_path)
    if rec is None:
        out["hive"] = None
        return out
    stream = vol.stream(rec, 0x80, "")
    if stream is None or stream.resident is not None:
        raise ReconError("hive has no non-resident unnamed data stream")
    hive = Hive(stream.read, stream.size)
    info = {"path": hive_path, "mft_record": rec, "size": stream.size,
            "runs": [{"lcn": l, "clusters": c} for l, c in stream.runs],
            "attribute_flags": stream.flags, "base_block": hive.base,
            "base_block_location": lbas_of(vol, stream, 0, 512)}
    out["hive"] = info
    info["bins_verified"] = hive.verify_bins()
    select = hive.open("Select")
    current = value_text(hive.value(select, "Current")) if select else None
    info["select"] = {n: value_text(hive.value(select, n)) for n in ("Current", "Default", "Failed", "LastKnownGood")} if select else None
    if not isinstance(current, int):
        raise ReconError("Select\\Current not found")
    cs = f"ControlSet{current:03d}"
    info["control_set"] = cs
    targets = {}
    services = {}
    for name in ("stornvme", "AppleAns"):
        key = hive.open(f"{cs}\\Services\\{name}")
        if key is None:
            services[name] = None
            continue
        s = {"key_cell": key["offset"]}
        for vn in ("Start", "ImagePath", "Type", "ErrorControl", "Group"):
            s[vn] = describe_value(vol, stream, hive.value(key, vn))
        so = hive.subkey(key, "StartOverride")
        s["StartOverride"] = None if so is None else {
            "key_cell": so["offset"], "values": [describe_value(vol, stream, v) for v in hive.values(so)]}
        services[name] = s
    info["services"] = services
    so = (services.get("stornvme") or {}).get("StartOverride")
    if so:
        for v in so["values"]:
            if v["name"] == "0" and v["data_file_offset"] is not None:
                targets["stornvme_StartOverride_0"] = (v["data_file_offset"], max(v["data_size"], 1))
    ip = (services.get("AppleAns") or {}).get("ImagePath")
    if ip and ip["data_file_offset"] is not None:
        targets["AppleAns_ImagePath"] = (ip["data_file_offset"], ip["data_size"])
        text = ip["value"] or ""
        marker = "appleans.inf_arm64_"
        i = text.lower().find(marker)
        if i >= 0 and old_store:
            cur = text[i + len(marker):i + len(marker) + 16]
            new = text[:i + len(marker)] + old_store + text[i + len(marker) + 16:]
            start = ip["data_file_offset"] + 2 * (i + len(marker))
            info["imagepath_patch"] = {
                "current_store_id": cur, "replacement_store_id": old_store,
                "same_length": len(old_store) == 16 and len(new) == len(text),
                "replacement_string": new, "bytes_file_offset": start, "bytes_length": 32,
                "old_bytes_hex": stream.read(start, 32).hex(),
                "new_bytes_hex": old_store.encode("utf-16-le").hex(),
                "location": lbas_of(vol, stream, start, 32)}
    # Transaction logs
    logs = {}
    for suffix in (".LOG1", ".LOG2"):
        lrec = vol.resolve(hive_path + suffix)
        if lrec is None:
            logs[suffix] = None
            continue
        ls = vol.stream(lrec, 0x80, "")
        logs[suffix] = {"mft_record": lrec, **parse_log(ls.read, ls.size, targets)}
    info["logs"] = logs
    pending = []
    for suffix, lg in logs.items():
        for e in (lg or {}).get("entries", []):
            if e["sequence"] >= hive.base["secondary_sequence"]:
                pending.append({"log": suffix, "sequence": e["sequence"], "touches": e["touches"]})
    info["log_entries_at_or_after_hive_sequence"] = pending
    # Driver store
    store = {"path": store_path, "packages": []}
    srec = vol.resolve(store_path)
    if srec is not None:
        for e in vol.listdir(srec):
            if e["namespace"] == 2 or not e["name"].lower().startswith("appleans.inf_"):
                continue
            pkg = {"directory": e["name"], "mft_record": e["ref"], "files": []}
            for f in vol.listdir(e["ref"]):
                if f["namespace"] == 2 or f["dir"]:
                    continue
                fs = vol.stream(f["ref"], 0x80, "")
                item = {"name": f["name"], "mft_record": f["ref"], "size": fs.size if fs else None}
                if fs is not None and fs.size <= 8 << 20:
                    item["sha256"] = sha256(fs.read(0, fs.size))
                pkg["files"].append(item)
            store["packages"].append(pkg)
    else:
        store["error"] = "driver store directory not found"
    out["driver_store"] = store
    return out


def recon(disk, hive_path, store_path, old_store):
    receipt = {"tool": "ans_offline_recon", "version": 1, "block_size": disk.block, "read_only": True}
    gpt = parse_gpt(disk)
    receipt["gpt"] = gpt
    volumes = []
    for part in gpt["partitions"]:
        try:
            if disk.read(part["first_lba"] * disk.block, 16)[3:11] != b"NTFS    ":
                continue
            volumes.append(recon_volume(disk, part, hive_path, store_path, old_store))
        except ReconError as e:
            volumes.append({"partition": part, "error": str(e)})
    receipt["volumes"] = volumes
    receipt["windows_volume_index"] = next(
        (i for i, v in enumerate(volumes) if v.get("hive")), None)
    receipt["blocks_read"] = len(disk.cache)
    return receipt


def main(argv=None):
    ap = argparse.ArgumentParser(description="Read-only reconnaissance of the Windows SYSTEM hive on the internal SSD")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--image", type=Path, help="disk image file (whole disk, 4096-byte blocks)")
    src.add_argument("--proxyclient", type=Path, help="m1n1 proxyclient directory; reads through the proxy")
    ap.add_argument("--output", type=Path, required=True, help="JSON receipt (must not exist)")
    ap.add_argument("--save-blocks", type=Path, help="directory that receives a copy of every block read")
    ap.add_argument("--hive", default="\\Windows\\System32\\config\\SYSTEM")
    ap.add_argument("--driver-store", default="\\Windows\\System32\\DriverStore\\FileRepository")
    ap.add_argument("--old-store-id", default="452f0c0cf11d36f5",
                    help="16 hex digits of the driver-store directory to go back to")
    a = ap.parse_args(argv)
    if a.output.exists():
        ap.error("refusing to overwrite an existing receipt")
    dev = ImageDevice(a.image) if a.image else ProxyDevice(a.proxyclient)
    try:
        disk = Disk(dev, a.save_blocks)
        try:
            receipt = recon(disk, a.hive, a.driver_store, a.old_store_id)
        except ReconError as e:
            receipt = {"tool": "ans_offline_recon", "error": str(e), "blocks_read": len(disk.cache)}
    finally:
        dev.close()
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(receipt, indent=1, default=lambda o: o.hex() if isinstance(o, bytes) else str(o)))
    w = receipt.get("windows_volume_index")
    print("RECON_ERROR " + receipt["error"] if "error" in receipt else
          f"RECON_OK volume={w} blocks_read={receipt['blocks_read']} -> {a.output}")
    return 0 if "error" not in receipt and w is not None else 3


if __name__ == "__main__":
    sys.exit(main())
