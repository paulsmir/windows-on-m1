"""Offline tests of tools/ans_repair/ans_offline_patch.py.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A write to the boot disk that changes anything but the named bytes, a write made although the
disk no longer matches what the reconnaissance saw, a dry run that writes, a missing backup,
or a write routine called in the wrong m1n1 image.
"""
import copy
import hashlib
import importlib.util
import json
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load(name, rel):
    spec = importlib.util.spec_from_file_location(name, ROOT / rel)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


P = load("ans_offline_patch", "tools/ans_repair/ans_offline_patch.py")
R = load("ans_offline_recon", "tools/ans_repair/ans_offline_recon.py")
T = load("recon_tests", "tests/test_ans_offline_recon.py")
BLOCK = 4096
SYS = "ab" * 32
BENCH_ELF = Path("/Volumes/pwdev/public_windows/.local/experiments/m1n1-s2v3-rq2-rst2abc/src/m1n1/build/m1n1.elf")


def diff_count(a, b):
    return sum(x != y for x, y in zip(a, b))


@unittest.skipUnless(shutil.which("mkntfs") and shutil.which("ntfscp"), "mkntfs/ntfscp not installed")
class ImageRehearsal(unittest.TestCase):
    """Reconnaissance receipt -> patch -> reconnaissance again, on a real NTFS volume."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="ans-patch-"))
        vol = cls.tmp / "vol.img"
        with open(vol, "wb") as f:
            f.truncate(64 << 20)
        run = lambda *a: subprocess.run(a, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        run("mkntfs", "-F", "-Q", "-q", "-s", "4096", "-c", "4096", str(vol))
        src = cls.tmp / "src.bin"
        src.write_bytes(T.build_hive())
        run("ntfscp", "-q", str(vol), str(src), "SYSTEM")
        cls.pristine = cls.tmp / "pristine.img"
        T.gpt_disk(vol.read_bytes(), cls.pristine)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix="case-", dir=self.tmp))
        self.disk = self.dir / "disk.img"
        shutil.copy(self.pristine, self.disk)
        self.receipt = self.recon()
        vol = self.receipt["volumes"][0]
        vol["driver_store"] = {"packages": [{"directory": "appleans.inf_arm64_" + T.OLD, "files": [
            {"name": "AppleAns.sys", "sha256": SYS}]}]}  # this volume has no driver store

    def recon(self):
        dev = R.ImageDevice(self.disk)
        try:
            r = R.recon(R.Disk(dev), "\\SYSTEM", "\\$Extend", T.OLD)
        finally:
            dev.close()
        return json.loads(json.dumps(r, default=lambda o: o.hex()))

    def run_plan(self, plan, execute):
        be = P.ImageBackend(self.disk)
        try:
            return P.apply(be, plan, self.dir / "backup", execute, lambda m: None)
        finally:
            be.close()

    def test_dry_run_writes_nothing(self):
        before = self.disk.read_bytes()
        res = self.run_plan(P.plan_imagepath(self.receipt, SYS), False)
        self.assertFalse(res["executed"])
        self.assertEqual(self.disk.read_bytes(), before)
        self.assertEqual(list((self.dir / "backup").iterdir()), [])

    def test_imagepath_changes_exactly_the_id(self):
        before = self.disk.read_bytes()
        plan = P.plan_imagepath(self.receipt, SYS)
        res = self.run_plan(plan, True)
        after = self.disk.read_bytes()
        self.assertTrue(res["executed"])
        self.assertEqual(len(after), len(before))
        changed = [i for i in range(len(before)) if before[i] != after[i]]
        lba, off = plan["steps"][0]["lba"], plan["steps"][0]["offset"]
        self.assertTrue(changed and all(lba * BLOCK + off <= i < lba * BLOCK + off + 32 for i in changed))
        hive = self.recon()["volumes"][0]["hive"]
        self.assertEqual(hive["services"]["AppleAns"]["ImagePath"]["value"], T.IMAGEPATH.replace(T.NEW, T.OLD))
        self.assertTrue(hive["base_block"]["checksum_ok"] and hive["base_block"]["clean"])
        self.assertEqual(hive["services"]["stornvme"]["StartOverride"]["values"][0]["value"], 3)
        backup = (self.dir / "backup" / f"lba-{lba:012d}.before.bin").read_bytes()
        self.assertEqual(backup, before[lba * BLOCK:(lba + 1) * BLOCK])
        self.assertEqual((self.dir / "backup" / f"lba-{lba:012d}.after.bin").read_bytes(), after[lba * BLOCK:(lba + 1) * BLOCK])
        # a second run sees a disk that no longer matches the receipt
        with self.assertRaises(P.PatchError):
            self.run_plan(plan, True)
        # restore brings back the identical disk
        be = P.ImageBackend(self.disk)
        try:
            r = P.restore(be, self.dir / "backup", lba, res["blocks"][0]["after_sha256"], True, lambda m: None)
        finally:
            be.close()
        self.assertTrue(r["executed"])
        self.assertEqual(self.disk.read_bytes(), before)

    def test_startoverride_changes_one_byte(self):
        before = self.disk.read_bytes()
        res = self.run_plan(P.plan_startoverride(self.receipt), True)
        after = self.disk.read_bytes()
        self.assertTrue(res["executed"])
        self.assertEqual(diff_count(before, after), 1)
        hive = self.recon()["volumes"][0]["hive"]
        self.assertEqual(hive["services"]["stornvme"]["StartOverride"]["values"][0]["value"], 0)
        self.assertEqual(hive["services"]["AppleAns"]["ImagePath"]["value"], T.IMAGEPATH)
        with self.assertRaises(P.PatchError):  # value is no longer 3
            P.plan_startoverride(self.recon())

    def test_refusals_before_any_write(self):
        before = self.disk.read_bytes()
        def bad(mutate, planner=lambda r: P.plan_imagepath(r, SYS)):
            r = copy.deepcopy(self.receipt)
            mutate(r["volumes"][0])
            with self.assertRaises(P.PatchError):
                planner(r)
        bad(lambda v: v["hive"]["base_block"].update(clean=False))
        bad(lambda v: v["hive"]["base_block"].update(checksum_ok=False))
        bad(lambda v: v["hive"].update(bins_verified=0))
        bad(lambda v: v["hive"].update(log_entries_at_or_after_hive_sequence=[{"log": ".LOG1", "sequence": 9, "touches": ["AppleAns_ImagePath"]}]))
        bad(lambda v: v["driver_store"]["packages"][0]["files"][0].update(sha256="00" * 32))
        bad(lambda v: v["driver_store"].update(packages=[]))
        bad(lambda v: v["hive"]["imagepath_patch"].update(same_length=False))
        bad(lambda v: v["hive"]["imagepath_patch"].update(new_bytes_hex="00" * 32))
        bad(lambda v: v["hive"]["services"]["stornvme"]["StartOverride"]["values"][0].update(data_hex="00000000"), P.plan_startoverride)
        bad(lambda v: v["hive"].update(log_entries_at_or_after_hive_sequence=[{"log": ".LOG2", "sequence": 9, "touches": ["stornvme_StartOverride_0"]}]), P.plan_startoverride)
        bad(lambda v: v["hive"]["services"].update(stornvme=None), P.plan_startoverride)
        with self.assertRaises(P.PatchError):
            P.hive_of({"tool": "ans_offline_recon", "error": "x"})
        # the disk changed after the reconnaissance: one other byte in the same block
        plan = P.plan_imagepath(self.receipt, SYS)
        st = plan["steps"][0]
        with open(self.disk, "r+b") as f:
            f.seek(st["lba"] * BLOCK + (st["offset"] + 100) % BLOCK)
            b = f.read(1); f.seek(-1, 1); f.write(bytes([b[0] ^ 1]))
        tampered = self.disk.read_bytes()
        with self.assertRaises(P.PatchError):
            self.run_plan(plan, True)
        self.assertEqual(self.disk.read_bytes(), tampered)
        self.assertEqual(diff_count(before, tampered), 1)
        self.assertFalse(any((self.dir / "backup").glob("*.after.bin")))

    def test_cli_dry_run_and_execute(self):
        rp = self.dir / "receipt.json"
        rp.write_text(json.dumps(self.receipt))
        before = self.disk.read_bytes()
        base = ["--mode", "startoverride", "--receipt", str(rp), "--image", str(self.disk), "--backup-dir", str(self.dir / "b")]
        self.assertEqual(P.main(base + ["--output", str(self.dir / "dry.json"), "--receipt-sha256", hashlib.sha256(rp.read_bytes()).hexdigest()]), 0)
        self.assertEqual(self.disk.read_bytes(), before)
        self.assertEqual(P.main(base + ["--output", str(self.dir / "bad.json"), "--receipt-sha256", "0" * 64, "--execute"]), 3)
        self.assertEqual(self.disk.read_bytes(), before)
        self.assertEqual(P.main(base + ["--output", str(self.dir / "run.json"), "--execute"]), 0)
        self.assertTrue(json.loads((self.dir / "run.json").read_text())["executed"])
        self.assertEqual(diff_count(before, self.disk.read_bytes()), 1)


class TwoBlockChange(unittest.TestCase):
    def test_bytes_across_two_blocks(self):
        tmp = Path(tempfile.mkdtemp(prefix="ans-patch2-"))
        self.addCleanup(shutil.rmtree, tmp, True)
        img = tmp / "d.img"
        data = bytearray(hashlib.sha256(b"x").digest() * (4 * BLOCK // 32))
        old, new = b"OLDOLDOL", b"newnewne"
        data[2 * BLOCK - 3:2 * BLOCK + 5] = old
        img.write_bytes(data)
        hashes = {1: P.sha256(bytes(data[BLOCK:2 * BLOCK])), 2: P.sha256(bytes(data[2 * BLOCK:3 * BLOCK]))}
        plan = P.build_steps([{"lba": 1, "offset": BLOCK - 3, "length": 3}, {"lba": 2, "offset": 0, "length": 5}],
                             old, new, hashes, "t")
        be = P.ImageBackend(img)
        try:
            self.assertTrue(P.apply(be, plan, tmp / "b", True, lambda m: None)["executed"])
        finally:
            be.close()
        want = bytes(data[:2 * BLOCK - 3]) + new + bytes(data[2 * BLOCK + 5:])
        self.assertEqual(img.read_bytes(), want)
        self.assertEqual(len(list((tmp / "b").glob("*.before.bin"))), 2)
        with self.assertRaises(P.PatchError):
            P.build_steps([{"lba": 1, "offset": BLOCK - 3, "length": 4}], old, new, hashes, "t")


class FakeM1n1:
    """Proxy, interface and utils in one: memory, a block store and call-by-address."""
    BASE = 0x810000000

    def __init__(self, sym, blocks):
        self.sym, self.blocks, self.mem, self.next = sym, blocks, {}, 0x900000000
        self.calls, self.corrupt_writemem, self.wrong_read, self.up = [], False, False, False
        for value, code in sym.values():
            self.mem[self.BASE + value] = bytearray(code)

    def get_base(self): return self.BASE
    def memalign(self, align, size):
        a = self.next; self.next += 0x10000; self.mem[a] = bytearray(size); return a
    def free(self, a): self.mem.pop(a, None)
    def readmem(self, a, n): return bytes(self.mem[a][:n])
    def writemem(self, a, d):
        self.mem[a][:len(d)] = d
        if self.corrupt_writemem: self.mem[a][7] ^= 0x40
    def nvme_init(self): self.up = True; return 1
    def nvme_shutdown(self): self.up = False
    def nvme_read(self, ns, lba, bfr): self.mem[bfr][:BLOCK] = self.blocks[lba]; return 1
    def nvme_flush(self, ns): self.calls.append(("flush",)); return 1
    def call(self, addr, *args):
        self.calls.append((addr - self.BASE,) + args)
        if addr == self.BASE + self.sym["nvme_read"][0]:
            data = self.blocks[args[1]]
            self.mem[args[2]][:BLOCK] = bytes(BLOCK) if self.wrong_read else data
        elif addr == self.BASE + self.sym["nvme_write"][0]:
            self.blocks[args[1]] = bytes(self.mem[args[2]][:BLOCK])
        else:
            raise AssertionError("call to an unknown address")
        return 0xdeadbeef00 | 1  # a bool return leaves the upper bits undefined


@unittest.skipUnless(BENCH_ELF.is_file(), "bench m1n1 ELF not present")
class ProxyPath(unittest.TestCase):
    def setUp(self):
        self.sym = P.elf_symbols(BENCH_ELF, P.CHECKED)
        self.blocks = {7: bytes(range(256)) * 16, 8: b"\x11" * BLOCK}
        self.m = FakeM1n1(self.sym, self.blocks)
        self.tmp = Path(tempfile.mkdtemp(prefix="ans-proxy-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)
        b = self.blocks[7]
        self.plan = P.build_steps([{"lba": 7, "offset": 100, "length": 4}], b[100:104], b"WXYZ", {7: P.sha256(b)}, "t")

    def backend(self):
        return P.ProxyBackend(self.m, self.m, self.m, BENCH_ELF)

    def test_symbols_match_the_linker_map(self):
        self.assertEqual((self.sym["nvme_read"][0], self.sym["nvme_write"][0]), (0x585c4, 0x586bc))
        self.assertEqual((len(self.sym["nvme_write"][1]), len(self.sym["nvme_rw_blocks"][1])), (0xf8, 0x1c0))
        with self.assertRaises(P.PatchError):
            P.elf_symbols(BENCH_ELF, ("no_such_function",))

    def test_elf_is_tied_to_the_bench_image_file(self):
        macho = BENCH_ELF.with_name("m1n1.macho")
        good = "a6916e26b5d56103106d59080c1d5a594f25780f897a6751f613804d5734fb86"
        P.check_image_file(BENCH_ELF, macho, good)
        with self.assertRaises(P.PatchError):
            P.check_image_file(BENCH_ELF, macho, "0" * 64)
        other = self.tmp / "other.macho"
        raw = bytearray(macho.read_bytes()); i = raw.find(self.sym["nvme_write"][1]); raw[i + 9] ^= 1
        other.write_bytes(raw)
        with self.assertRaises(P.PatchError):
            P.check_image_file(BENCH_ELF, other, P.sha256(bytes(raw)))

    def test_dry_run_never_calls_write(self):
        be = self.backend()
        res = P.apply(be, self.plan, self.tmp / "b", False, lambda m: None)
        be.close()
        self.assertEqual(res["preflight"]["nvme_write"], "0x586bc")
        self.assertEqual([c[0] for c in self.m.calls], [0x585c4])
        self.assertEqual(self.blocks[7], bytes(range(256)) * 16)
        self.assertFalse(self.m.up)

    def test_execute_writes_one_block_through_nvme_write(self):
        be = self.backend()
        res = P.apply(be, self.plan, self.tmp / "b", True, lambda m: None)
        be.close()
        want = bytearray(bytes(range(256)) * 16); want[100:104] = b"WXYZ"
        self.assertTrue(res["executed"])
        self.assertEqual(self.blocks[7], bytes(want))
        self.assertEqual(self.blocks[8], b"\x11" * BLOCK)
        writes = [c for c in self.m.calls if c[0] == 0x586bc]
        self.assertEqual([(c[1], c[2]) for c in writes], [(1, 7)])
        self.assertIn(("flush",), self.m.calls)

    def test_wrong_image_or_convention_stops_before_the_write(self):
        self.m.mem[self.m.BASE + 0x586bc][0xf0] ^= 1  # late in the function
        be = self.backend()
        with self.assertRaises(P.PatchError):
            P.apply(be, self.plan, self.tmp / "b1", True, lambda m: None)
        be.close()
        self.m = FakeM1n1(self.sym, self.blocks); self.m.wrong_read = True
        be = self.backend()
        with self.assertRaises(P.PatchError):
            P.apply(be, self.plan, self.tmp / "b2", True, lambda m: None)
        be.close()
        self.m = FakeM1n1(self.sym, self.blocks); self.m.corrupt_writemem = True
        be = self.backend()
        with self.assertRaises(P.PatchError):
            P.apply(be, self.plan, self.tmp / "b3", True, lambda m: None)
        be.close()
        self.assertEqual(self.blocks[7], bytes(range(256)) * 16)
        self.assertFalse(any(c[0] == 0x586bc for c in self.m.calls))


if __name__ == "__main__":
    unittest.main()
