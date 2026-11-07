"""HVS2ISO002 census tool: offline contract on a fake proxy and a fake clock.

Invariants caught: the tool reads exactly the preregistered pages and nothing else (no
write API is reachable), detects a single changed page, honours the pass schedule and
budget, fails closed on a proxy fault, and refuses on a live launcher, reachable guest,
stale proxy or ADT drift.
"""

import hashlib
import subprocess
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import hv_carveout_census as cc  # noqa: E402

TOTAL = sum((b - a) // cc.PAGE for a, b in cc.RANGES)


class FakeClock:
    def __init__(self, per_read=0.001):
        self.t = 1000.0
        self.per_read = per_read
        self.sleeps = []

    def __call__(self):
        return self.t

    def sleep(self, s):
        self.sleeps.append(s)
        self.t += s


class FakeMemory:
    """Only read access exists; any other attribute use fails the test."""

    def __init__(self, clock, mutate=None):
        self.clock, self.mutate, self.reads, self.passno = clock, mutate, [], 0

    def read_chunk(self, addr, size):
        self.reads.append((addr, size))
        self.clock.t += self.clock.per_read
        data = bytearray(size)
        for off in range(0, size, cc.PAGE):
            if (addr + off) % (8 * cc.PAGE) == 0:
                data[off] = 0xA5  # some non-zero pages
        if self.mutate and self.mutate[0] <= self.passno and addr <= self.mutate[1] < addr + size:
            data[self.mutate[1] - addr + 7] ^= 0xFF
        if addr + size == cc.RANGES[-1][1]:
            self.passno += 1
        return bytes(data)


class CensusTests(unittest.TestCase):
    def run_census(self, **kw):
        clock = FakeClock(kw.pop("per_read", 0.001))
        mem = FakeMemory(clock, kw.pop("mutate", None))
        r = cc.census(mem.read_chunk, clock=clock, sleep=clock.sleep, **kw)
        return r, mem, clock

    def test_reads_exactly_the_preregistered_pages(self):
        r, mem, _ = self.run_census()
        self.assertEqual(r["verdict"], "STABLE")
        self.assertTrue(r["complete"])
        self.assertEqual(r["page_count"], TOTAL)
        self.assertEqual(TOTAL, (0xfa0000 + 0xb518000) // cc.PAGE)
        covered = set()
        for addr, size in mem.reads:
            self.assertTrue(any(a <= addr and addr + size <= b for a, b in cc.RANGES))
            self.assertLessEqual(size, cc.CHUNK)
            covered.update(range(addr, addr + size, cc.PAGE))
        self.assertEqual(covered, set(cc.pages()))
        for p in r["passes"]:
            self.assertEqual(len(p["pages"]), TOTAL)
        self.assertEqual(r["nonzero_pages_pass0"], sum(1 for pa in cc.pages() if pa % (8 * cc.PAGE) == 0))

    def test_hash_and_flag_per_page(self):
        r, _, _ = self.run_census()
        first = cc.RANGES[0][0]
        h, nz = r["passes"][0]["pages"][hex(first + cc.PAGE)]
        self.assertEqual(h, hashlib.sha256(bytes(cc.PAGE)).hexdigest())
        self.assertFalse(nz)
        self.assertTrue(r["passes"][0]["pages"][hex(first)][1])

    def test_single_changed_page_is_reported(self):
        target = 0x86d094000 + 3 * cc.PAGE
        r, _, _ = self.run_census(mutate=(2, target))
        self.assertEqual(r["verdict"], "CHANGED")
        self.assertEqual(r["changed_pages"], [{"pa": hex(target), "pass": 2}])

    def test_pass_schedule(self):
        r, _, clock = self.run_census()
        self.assertEqual([p["start_s"] for p in r["passes"]][0], 0)
        self.assertGreaterEqual(r["passes"][1]["start_s"], 60)
        self.assertGreaterEqual(r["passes"][2]["start_s"], 180)
        self.assertLess(r["passes"][2]["start_s"], 181)

    def test_budget_overrun_is_incomplete(self):
        r, _, _ = self.run_census(per_read=1.0, budget=300)  # 784 chunks * 1 s per pass
        self.assertEqual(r["verdict"], "INCOMPLETE")
        self.assertIn("exceeded 300 s", r["stop"])

    def test_fault_is_incomplete(self):
        def faulting(addr, size):
            raise cc.Fault(f"memory fault reading {addr:#x}")
        clock = FakeClock()
        r = cc.census(faulting, clock=clock, sleep=clock.sleep)
        self.assertEqual(r["verdict"], "INCOMPLETE")
        self.assertEqual(r["passes"], [])

    def test_guarded_reader_checks_exception_counter_and_window(self):
        state = SimpleNamespace(count=0, guard=None)
        p = SimpleNamespace(set_exc_guard=lambda m: setattr(state, "guard", m),
                            get_exc_count=lambda: state.count)

        def readmem(addr, size):
            state.count += 1 if addr == 0x864000000 else 0
            return bytes(size)
        reader = cc.guarded_chunk_reader(p, SimpleNamespace(readmem=readmem), "G")
        self.assertEqual(state.guard, "G")
        self.assertEqual(reader(0x860500000, cc.PAGE), bytes(cc.PAGE))
        with self.assertRaises(cc.Fault):
            reader(0x864000000, cc.PAGE)
        with self.assertRaises(cc.Fault):
            reader(0x9df704000, 0x8000)  # crosses mem_top

    def test_short_read_fails_closed(self):
        clock = FakeClock()
        with self.assertRaises(cc.Fault):
            cc.hash_pass(lambda a, s: bytes(s - 1), clock=clock)


class PreconditionTests(unittest.TestCase):
    def test_run_uefi_detection(self):
        alive = lambda *a, **k: SimpleNamespace(stdout="4711\n", returncode=0)
        dead = lambda *a, **k: SimpleNamespace(stdout="", returncode=1)
        self.assertEqual(cc.run_uefi_alive(alive), ["4711"])
        self.assertEqual(cc.run_uefi_alive(dead), [])

    def test_ssh_detection(self):
        up = lambda *a, **k: SimpleNamespace(stdout="up\n", returncode=0)
        down = lambda *a, **k: SimpleNamespace(stdout="", returncode=255)

        def hang(*a, **k):
            raise subprocess.TimeoutExpired("ssh", 20)
        self.assertTrue(cc.guest_ssh_up("x", up))
        self.assertFalse(cc.guest_ssh_up("x", down))
        self.assertFalse(cc.guest_ssh_up("x", hang))

    def test_banner_freshness(self):
        fresh = "TTY> boot_args at 0x805548000\nTTY> Running proxy...\n"
        self.assertTrue(cc.banner_fresh(fresh))
        self.assertFalse(cc.banner_fresh("Fetching ADT (0x0005C000 bytes)...\n"))
        saved = Path("/Volumes/pwdev/public_windows/.worktrees/ans-native/.local/ans-native/"
                     "ANSDART001-R3/banner.txt")
        if saved.is_file():
            self.assertTrue(cc.banner_fresh(saved.read_text()))

    def test_adt_drift_refused(self):
        R = lambda a, b: SimpleNamespace(start=a, end=b)
        cc.check_ranges([R(*r) for r in cc.RANGES])
        with self.assertRaises(cc.Fault):
            cc.check_ranges([R(*cc.RANGES[0])])

    def test_ranges_match_policy_on_fixture(self):
        trees = [ROOT / "m1n1_windows", ROOT.parent / "m1n1-stage2-above-ram"]
        trees = [m for m in trees if (m / "tests/data/j313_adt_above_ram.json").is_file()]
        if not trees:
            self.skipTest("no m1n1_windows tree at feature/hv-stage2-above-ram")
        m1n1 = trees[0]
        sys.path.insert(0, str(m1n1 / "tests"))
        sys.path.insert(0, str(m1n1 / "proxyclient"))
        import hv_stage2_above_ram_test as t
        from m1n1.hv.stage2_policy import in_ram_carveouts
        cc.check_ranges(in_ram_carveouts(t.FakeAdt(t.load_fixture()), *cc.GUEST_RAM))

    def test_source_has_no_write_or_mmio_calls(self):
        src = (ROOT / "tools/hv_carveout_census.py").read_text()
        code = src.split('"""', 2)[2]  # skip the module docstring
        for forbidden in ("writemem", "write32", "write64", "write16", "write8", "memset",
                          "memcpy", "m1n1.setup", "mmio", "pmgr", "reboot", "smp_", "hv_"):
            self.assertNotIn(forbidden, code.lower(), forbidden)


if __name__ == "__main__":
    unittest.main()
