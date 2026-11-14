"""Offline tests of tools/ans_repair/ans_offline_recon.py.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A wrong disk location (LBA / byte offset) for the registry bytes an offline repair would
patch, a mis-parsed hive or NTFS structure, or a torn record accepted as valid.  The NTFS part
runs against a real volume made by mkntfs/ntfscp (skipped when those tools are absent) and,
when a real SYSTEM hive is available locally (ANS_RECON_TEST_HIVE or the ans-native copy), on
that hive; the synthetic hive always runs.
"""
import hashlib
import importlib.util
import json
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("ans_offline_recon", ROOT / "tools/ans_repair/ans_offline_recon.py")
R = importlib.util.module_from_spec(spec)
spec.loader.exec_module(R)
BLOCK = 4096
PART_LBA = 256


# ---- synthetic hive (independent writer: cells laid out by hand) ---------------------------
class HiveBuilder:
    def __init__(self):
        self.bins = bytearray()

    def cell(self, data):
        size = (4 + len(data) + 7) & ~7
        assert size <= 4096 - 32
        used = len(self.bins) % 4096
        if used and used + size > 4096:  # close the bin with one free cell
            rest = 4096 - used
            self.bins += struct.pack("<i", rest) + bytes(rest - 4)
        if len(self.bins) % 4096 == 0:
            self.bins += b"hbin" + struct.pack("<II", len(self.bins), 4096) + bytes(20)
        off = len(self.bins)
        self.bins += struct.pack("<i", -size) + data + bytes(size - 4 - len(data))
        return off

    def vk(self, name, vtype, data, ascii_name=True):
        nm = name.encode("latin-1") if ascii_name else name.encode("utf-16-le")
        if len(data) <= 4:
            size, off = len(data) | 0x80000000, int.from_bytes(data.ljust(4, b"\0"), "little")
        else:
            size, off = len(data), self.cell(data)
        return self.cell(b"vk" + struct.pack("<HIIIHH", len(nm), size, off, vtype, 1 if ascii_name else 0, 0) + nm)

    def nk(self, name, subkeys=(), values=(), list_sig=b"lh"):
        vlist = self.cell(b"".join(struct.pack("<I", v) for v in values)) if values else 0xffffffff
        if subkeys and list_sig == b"ri":
            half = len(subkeys) // 2 or 1
            parts = [subkeys[:half], subkeys[half:]]
            leaves = [self.cell(b"li" + struct.pack("<H", len(p)) + b"".join(struct.pack("<I", s) for s in p)) for p in parts if p]
            slist = self.cell(b"ri" + struct.pack("<H", len(leaves)) + b"".join(struct.pack("<I", l) for l in leaves))
        elif subkeys:
            slist = self.cell(list_sig + struct.pack("<H", len(subkeys)) + b"".join(struct.pack("<II", s, 0) for s in subkeys))
        else:
            slist = 0xffffffff
        nm = name.encode("latin-1")
        body = b"nk" + struct.pack("<HQII", 0x20, 0, 0, 0) + struct.pack("<6I", len(subkeys), 0, slist, 0xffffffff, len(values), vlist)
        body += struct.pack("<7I", 0, 0xffffffff, 0, 0, 0, 0, 0) + struct.pack("<HH", len(nm), 0) + nm
        return self.cell(body)

    def finish(self, root, seq=(7, 7)):
        rest = -len(self.bins) % 4096
        if rest:
            self.bins += struct.pack("<i", rest) + bytes(rest - 4)
        base = bytearray(4096)
        base[:4] = b"regf"
        struct.pack_into("<II", base, 4, *seq)
        struct.pack_into("<7I", base, 20, 1, 5, 0, 1, root, len(self.bins), 1)
        base[48:48 + 12] = "SYSTEM".encode("utf-16-le")
        struct.pack_into("<I", base, 508, R.regf_checksum(bytes(base)))
        return bytes(base) + bytes(self.bins)


OLD, NEW = "452f0c0cf11d36f5", "0123456789abcdef"
IMAGEPATH = f"\\SystemRoot\\System32\\DriverStore\\FileRepository\\appleans.inf_arm64_{NEW}\\AppleAns.sys"


def build_hive(seq=(7, 7)):
    b = HiveBuilder()
    dword = lambda v: struct.pack("<I", v)
    override = b.nk("StartOverride", values=[b.vk("0", 4, dword(3))])
    stornvme = b.nk("stornvme", subkeys=[override], values=[b.vk("Start", 4, dword(0)),
                    b.vk("ImagePath", 2, "System32\\drivers\\stornvme.sys\0".encode("utf-16-le"))])
    # pad so that the AppleAns ImagePath data cell straddles nothing special, then the key
    apple = b.nk("AppleAns", values=[b.vk("Start", 4, dword(0)), b.vk("ImagePath", 2, (IMAGEPATH + "\0").encode("utf-16-le")),
                                     b.vk("Group", 1, "SCSI Miniport\0".encode("utf-16-le"), ascii_name=False)])
    filler = [b.nk(f"svc{i:03d}") for i in range(40)]
    services = b.nk("Services", subkeys=filler[:20] + [apple, stornvme] + filler[20:], list_sig=b"ri")
    cs1 = b.nk("ControlSet001", subkeys=[services], list_sig=b"lf")
    select = b.nk("Select", values=[b.vk("Current", 4, dword(1)), b.vk("Default", 4, dword(1)),
                                    b.vk("Failed", 4, dword(0)), b.vk("LastKnownGood", 4, dword(1))])
    root = b.nk("ROOT", subkeys=[cs1, select])
    return b.finish(root, seq)


class HiveTests(unittest.TestCase):
    def setUp(self):
        self.raw = build_hive()
        self.hive = R.Hive(lambda o, n: self.raw[o:o + n], len(self.raw))

    def test_base_block(self):
        b = self.hive.base
        self.assertTrue(b["clean"] and b["checksum_ok"])
        self.assertEqual(b["file_name"], "SYSTEM")
        dirty = build_hive(seq=(8, 7))
        self.assertFalse(R.parse_base_block(dirty[:4096])["clean"])
        bad = bytearray(self.raw[:4096]); bad[40] ^= 1
        self.assertFalse(R.parse_base_block(bytes(bad))["checksum_ok"])
        with self.assertRaises(R.ReconError):
            R.parse_base_block(b"nope" + bytes(4092))

    def test_checksum_special_values(self):
        self.assertEqual(R.regf_checksum(bytes(508)), 1)
        self.assertEqual(R.regf_checksum(b"\xff\xff\xff\xff" + bytes(504)), 0xfffffffe)

    def test_values_and_offsets(self):
        self.assertEqual(R.value_text(self.hive.value(self.hive.open("Select"), "current")), 1)
        so = self.hive.open("ControlSet001\\Services\\stornvme\\StartOverride")
        v = self.hive.value(so, "0")
        self.assertTrue(v["inline"])
        self.assertEqual((v["type"], R.value_text(v)), (4, 3))
        # the reported file offset really holds the inline DWORD
        self.assertEqual(self.raw[v["data_file_offset"]:v["data_file_offset"] + 4], struct.pack("<I", 3))
        ip = self.hive.value(self.hive.open("controlset001\\services\\APPLEANS"), "ImagePath")
        self.assertEqual(R.value_text(ip), IMAGEPATH)
        off = ip["data_file_offset"] + 2 * IMAGEPATH.index(NEW)
        self.assertEqual(self.raw[off:off + 32].decode("utf-16-le"), NEW)
        self.assertEqual(R.value_text(self.hive.value(self.hive.open("ControlSet001\\Services\\AppleAns"), "Group")), "SCSI Miniport")
        self.assertIsNone(self.hive.open("ControlSet001\\Services\\absent"))
        self.assertEqual(len(self.hive.subkeys(self.hive.open("ControlSet001\\Services"))), 42)

    def test_bins_are_walked(self):
        self.assertEqual(self.hive.verify_bins(), (len(self.raw) - 4096) // 4096)
        raw = bytearray(self.raw); raw[4096 + 4096 + 4] ^= 0x10  # second bin claims another offset
        with self.assertRaises(R.ReconError):
            R.Hive(lambda o, n: bytes(raw[o:o + n]), len(raw)).verify_bins()

    def test_damage_is_refused(self):
        raw = bytearray(self.raw)
        key = self.hive.open("ControlSet001\\Services\\stornvme")
        struct.pack_into("<i", raw, key["file_offset"], 64)  # the key cell marked free
        h = R.Hive(lambda o, n: bytes(raw[o:o + n]), len(raw))
        with self.assertRaises(R.ReconError):
            h.open("ControlSet001\\Services\\stornvme")

    def test_log_entries(self):
        v = self.hive.value(self.hive.open("ControlSet001\\Services\\stornvme\\StartOverride"), "0")
        page = (v["data_file_offset"] - 4096) & ~4095
        base = bytearray(self.raw[:512]); struct.pack_into("<I", base, 28, 6)
        def entry(seq, pages):
            body = b"".join(struct.pack("<II", off, 4096) for off in pages) + b"".join(bytes(4096) for _ in pages)
            size = (40 + len(body) + 511) & ~511
            return (b"HvLE" + struct.pack("<5I", size, 0, seq, len(self.raw) - 4096, len(pages)) + bytes(16) + body).ljust(size, b"\0")
        log = bytes(base) + entry(7, [page]) + entry(8, [page + 8192]) + bytes(2048)
        r = R.parse_log(lambda o, n: log[o:o + n], len(log), {"t": (v["data_file_offset"], 4)})
        self.assertEqual([e["sequence"] for e in r["entries"]], [7, 8])
        self.assertEqual([e["touches"] for e in r["entries"]], [["t"], []])
        self.assertEqual(R.parse_log(lambda o, n: b"", 0, {})["note"], "empty")


class StructureTests(unittest.TestCase):
    def test_data_runs(self):
        self.assertEqual(R.decode_runs(bytes.fromhex("21 18 34 56 00".replace(" ", ""))), [(0x5634, 0x18)])
        # second run relative and negative, then a sparse run
        self.assertEqual(R.decode_runs(bytes.fromhex("1110201108f8011000".replace(" ", ""))[:8]),
                         [(0x20, 0x10), (0x18, 0x08), (None, 0x10)])
        with self.assertRaises(R.ReconError):
            R.decode_runs(b"\x29\x01")

    def test_fixup_detects_torn_record(self):
        rec = bytearray(1024)
        rec[:4] = b"FILE"; struct.pack_into("<HH", rec, 4, 48, 3)
        rec[48:50] = b"\x05\x00"; rec[50:52] = b"AB"; rec[52:54] = b"CD"
        rec[510:512] = b"\x05\x00"; rec[1022:1024] = b"\x05\x00"
        out = R.fixup(bytes(rec), "t")
        self.assertEqual((out[510:512], out[1022:1024]), (b"AB", b"CD"))
        rec[1022] = 4
        with self.assertRaises(R.ReconError):
            R.fixup(bytes(rec), "t")


def gpt_disk(volume, path):
    """Whole-disk image: protective MBR, GPT with one basic-data partition at PART_LBA."""
    blocks = len(volume) // BLOCK
    entry = bytearray(128)
    entry[:16] = bytes.fromhex("a2a0d0ebe5b9334487c068b6b72699c7")
    entry[16:32] = bytes(range(16))
    struct.pack_into("<QQ", entry, 32, PART_LBA, PART_LBA + blocks - 1)
    entry[56:56 + 16] = "Windows ".encode("utf-16-le")
    entries = bytes(entry) + bytes(127 * 128)
    hdr = bytearray(BLOCK)
    hdr[:8] = b"EFI PART"
    struct.pack_into("<IIII", hdr, 8, 0x10000, 92, 0, 0)
    struct.pack_into("<QQQQ", hdr, 24, 1, PART_LBA + blocks + 8, 6, PART_LBA + blocks)
    hdr[56:72] = bytes(range(16, 32))
    struct.pack_into("<QIII", hdr, 72, 2, 128, 128, zlib.crc32(entries))
    struct.pack_into("<I", hdr, 16, zlib.crc32(bytes(hdr[:92])))
    with open(path, "wb") as f:
        f.write(bytes(BLOCK) + bytes(hdr) + entries.ljust(BLOCK * (PART_LBA - 2), b"\0") + volume + bytes(9 * BLOCK))


def real_hive():
    for p in (os.environ.get("ANS_RECON_TEST_HIVE"),
              ROOT.parents[2] / ".worktrees/ans-native/.local/ans-native/ans010/hive/SYSTEM",
              "/Volumes/pwdev/public_windows/.worktrees/ans-native/.local/ans-native/ans010/hive/SYSTEM"):
        if p and Path(p).is_file():
            return Path(p)
    return None


@unittest.skipUnless(shutil.which("mkntfs") and shutil.which("ntfscp") and shutil.which("ntfscat"),
                     "mkntfs/ntfscp/ntfscat not installed")
class RealNtfsTests(unittest.TestCase):
    """A volume written by ntfs-3g's tools (independent of this parser), 4096-byte sectors."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="ans-recon-"))
        vol = cls.tmp / "vol.img"
        with open(vol, "wb") as f:
            f.truncate(96 << 20)
        run = lambda *a: subprocess.run(a, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        run("mkntfs", "-F", "-Q", "-q", "-s", "4096", "-c", "4096", "-L", "WINTEST", str(vol))
        cls.files = {}
        src = cls.tmp / "src.bin"
        for i in range(150):  # grows the root index past one block and fragments free space
            data = hashlib.sha256(str(i).encode()).digest() * (90 + i * 3)
            src.write_bytes(data)
            name = f"filler-file-with-a-long-name-{i:03d}.bin"
            run("ntfscp", "-q", str(vol), str(src), name)
            cls.files[name] = data
        cls.hive_src = real_hive()
        hive = cls.hive_src.read_bytes() if cls.hive_src else build_hive()
        src.write_bytes(hive)
        run("ntfscp", "-q", str(vol), str(src), "SYSTEM")
        cls.files["SYSTEM"] = hive
        log = build_hive()[:512] + bytes(8192)
        src.write_bytes(log)
        run("ntfscp", "-q", str(vol), str(src), "SYSTEM.LOG1")
        cls.vol_path, cls.disk_path = vol, cls.tmp / "disk.img"
        gpt_disk(vol.read_bytes(), cls.disk_path)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def open(self, path=None):
        dev = R.ImageDevice(path or self.disk_path)
        self.addCleanup(dev.close)
        return R.Disk(dev)

    def test_gpt(self):
        g = R.parse_gpt(self.open())
        self.assertTrue(g["header_crc_ok"] and g["entries_crc_ok"])
        self.assertEqual(g["partitions"][0]["type"], "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7")
        self.assertEqual((g["partitions"][0]["first_lba"], g["partitions"][0]["name"]), (PART_LBA, "Windows "))

    def test_files_match_their_sources(self):
        vol = R.Ntfs(self.open(), PART_LBA)
        self.assertEqual((vol.sector, vol.cluster), (4096, 4096))
        for name, data in self.files.items():
            rec = vol.resolve("\\" + name)
            self.assertIsNotNone(rec, name)
            s = vol.stream(rec, 0x80, "")
            self.assertEqual(s.size, len(data), name)
            self.assertEqual(hashlib.sha256(s.read(0, s.size)).digest(), hashlib.sha256(data).digest(), name)
        self.assertIsNone(vol.resolve("\\no-such-file"))
        self.assertIsNone(vol.resolve("\\filler-file-with-a-long-name-0"))
        # case-insensitive, and a nested path through a directory mkntfs created
        self.assertEqual(vol.resolve("\\system"), vol.resolve("\\SYSTEM"))
        self.assertIsNotNone(vol.resolve("\\$Extend\\$Reparse"))
        self.assertIsNone(vol.resolve("\\$Extend\\SYSTEM"))

    def test_listing_matches_ntfsls(self):
        vol = R.Ntfs(self.open(), PART_LBA)
        mine = sorted(e["name"] for e in vol.listdir(5) if e["namespace"] != 2 and not e["name"].startswith("$") and e["name"] != ".")
        theirs = sorted(subprocess.run(["ntfsls", str(self.vol_path)], capture_output=True, text=True).stdout.split())
        self.assertEqual(mine, theirs)
        self.assertEqual(len(mine), len(self.files) + 1)

    def test_locations_are_the_bytes_on_disk(self):
        disk = self.open()
        vol = R.Ntfs(disk, PART_LBA)
        s = vol.stream(vol.resolve("\\SYSTEM"), 0x80, "")
        raw = self.disk_path.read_bytes()
        data = self.files["SYSTEM"]
        for off, n in ((0, 4), (4090, 20), (len(data) - 5000, 5000), (123456, 9000)):
            got = b"".join(raw[p["lba"] * BLOCK + p["offset"]:p["lba"] * BLOCK + p["offset"] + p["length"]]
                           for p in R.lbas_of(vol, s, off, n))
            self.assertEqual(got, data[off:off + n])
            self.assertTrue(all(p["offset"] + p["length"] <= BLOCK for p in R.lbas_of(vol, s, off, n)))
        with self.assertRaises(R.ReconError):
            s.locate(len(data) - 1, 2)

    def test_receipt_and_patch_rehearsal(self):
        receipt = R.recon(self.open(), "\\SYSTEM", "\\$Extend", OLD)
        json.dumps(receipt, default=lambda o: o.hex() if isinstance(o, bytes) else str(o))
        self.assertEqual(receipt["windows_volume_index"], 0)
        hive = receipt["volumes"][0]["hive"]
        self.assertTrue(hive["base_block"]["checksum_ok"])
        self.assertEqual(hive["logs"][".LOG2"], None)
        self.assertEqual(hive["logs"][".LOG1"]["entries"], [])
        apple = hive["services"]["AppleAns"]
        self.assertIn("appleans.inf_arm64_", apple["ImagePath"]["value"])
        if self.hive_src:
            # recorded independently with Windows' own registry tools (ans010/hive/*.txt)
            self.assertIn(apple["ImagePath"]["value"].split("appleans.inf_arm64_")[1][:16],
                          ("68a73bcd7b60fbb4", "91f16a36b2b7bc6d"))
        else:
            self.assertEqual(hive["services"]["stornvme"]["StartOverride"]["values"][0]["value"], 3)
        patch = hive["imagepath_patch"]
        self.assertTrue(patch["same_length"])
        # Rehearsal of the in-place repair on a copy: only the named bytes change, and both this
        # parser and ntfscat then see the replacement string in an otherwise identical file.
        copy = self.tmp / "patched.img"
        raw = bytearray(self.disk_path.read_bytes())
        new = bytes.fromhex(patch["new_bytes_hex"])
        pos = 0
        for part in patch["location"]:
            a = part["lba"] * BLOCK + part["offset"]
            self.assertEqual(raw[a:a + part["length"]].hex(), patch["old_bytes_hex"][2 * pos:2 * (pos + part["length"])])
            raw[a:a + part["length"]] = new[pos:pos + part["length"]]
            pos += part["length"]
        self.assertEqual(pos, 32)
        copy.write_bytes(raw)
        after = R.recon(self.open(copy), "\\SYSTEM", "\\$Extend", OLD)["volumes"][0]["hive"]
        self.assertEqual(after["services"]["AppleAns"]["ImagePath"]["value"], patch["replacement_string"])
        self.assertTrue(after["base_block"]["checksum_ok"])
        vol_copy = self.tmp / "patched-vol.img"
        vol_copy.write_bytes(raw[PART_LBA * BLOCK:PART_LBA * BLOCK + self.vol_path.stat().st_size])
        seen = subprocess.run(["ntfscat", str(vol_copy), "SYSTEM"], capture_output=True).stdout
        want = bytearray(self.files["SYSTEM"])
        want[patch["bytes_file_offset"]:patch["bytes_file_offset"] + 32] = new
        self.assertEqual(hashlib.sha256(seen).hexdigest(), hashlib.sha256(bytes(want)).hexdigest())


if __name__ == "__main__":
    unittest.main()
