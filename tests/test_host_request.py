import pathlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from tools import host_request  # noqa: E402


class FakeHeap:
    def malloc(self, size):
        return 0x1000

    def free(self, address):
        pass


class FakeIface:
    def readmem(self, address, size):
        return struct.pack("<QQIIIIII", 2, 0, 1, 3, 4, 0, 0, 77)


class FakeProxy:
    def __init__(self):
        self.calls = []
        self.heap = FakeHeap()
        self.iface = FakeIface()

    def request(self, op, *args, **kwargs):
        self.calls.append((op, args))
        return 0 if op != host_request.P_DISPLAY_SCANOUT_RECOVERY_STATUS else 1


class HostRequestTests(unittest.TestCase):
    def test_request_file_is_consumed_once(self):
        proxy = FakeProxy()
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "req.txt"
            path.write_text("refuse 1\n# comment\ndrop 2\nstatus\n")
            lines = host_request.consume(path, proxy)
            self.assertEqual([c[0] for c in proxy.calls],
                             [0x110B, 0x110C, 0x110D])
            self.assertEqual(proxy.calls[0][1], (1,))
            self.assertIn("abandoned_swap_id=77", lines[2])
            self.assertFalse(path.exists())
            again = host_request.consume(path, proxy)
            self.assertEqual(again, ["HOST_REQUEST none (no request file)"])
            self.assertEqual(len(proxy.calls), 3)

    def test_malformed_file_arms_nothing(self):
        proxy = FakeProxy()
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "req.txt"
            path.write_text("refuse 1\nrefuse lots\n")
            lines = host_request.consume(path, proxy)
            self.assertTrue(lines[0].startswith("HOST_REQUEST rejected"))
            self.assertEqual(proxy.calls, [])

    def test_count_is_bounded(self):
        with self.assertRaises(ValueError):
            host_request.parse(f"drop {host_request.MAX_COUNT + 1}")


class MmuCfgVerb(unittest.TestCase):
    class Proxy:
        def __init__(self):
            self.reads, self.writes = [], []

        def read32(self, address):
            self.reads.append(address)
            return {0x204D08000: 1, 0x204D0800C: 0x003680D8, 0x204D08010: 0x003680D8,
                    0x204D08024: 0x0027FFEE}.get(address, 0)

        def write32(self, address, value):
            self.writes.append((address, value))

    def test_parse(self):
        self.assertEqual(host_request.parse("mmucfg"), [("mmucfg", 0)])
        with self.assertRaises(ValueError):
            host_request.parse("mmucfg 1")

    def test_reads_exactly_the_twelve_words_and_writes_nothing(self):
        proxy = self.Proxy()
        lines = host_request.execute(proxy, host_request.parse("mmucfg"))
        self.assertEqual(proxy.reads, [0x204D08000 + 4 * i for i in range(12)])
        self.assertEqual(proxy.writes, [])
        # the EXP2235 line, word for word
        self.assertEqual(lines, ["HOST_REQUEST mmucfg 00000001 00000000 00000000 003680d8 003680d8 "
                                 "00000000 00000000 00000000 00000000 0027ffee 00000000 00000000"])


class MccPlanesVerb(unittest.TestCase):
    class Proxy:
        def __init__(self, enable, status):
            self.enable, self.status, self.reads, self.writes = enable, status, [], []

        def read32(self, address):
            self.reads.append(address)
            return self.enable if address & 0xFFFF == 0x1C00 else self.status

        def write32(self, address, value):
            self.writes.append((address, value))

    def test_reads_enable_and_status_of_four_planes_and_writes_nothing(self):
        # enabled: 16 ways, status data count 16 (bits 14:10) and tag count 16 (bits 9:5)
        proxy = self.Proxy(16, (16 << 10) | (16 << 5))
        lines = host_request.execute(proxy, host_request.parse("mccplanes"))
        self.assertEqual(proxy.reads, [0x200000000 + p * 0x40000 + o for p in range(4) for o in (0x1C00, 0x1C04)])
        self.assertEqual(proxy.writes, [])
        self.assertEqual(lines[0].count("enable=0x10,status=0x4200,data_ways=16,tag_ways=16"), 4)

    def test_the_state_iboot_leaves_reads_as_zero_ways(self):
        # EXP2242: cache_enable=0x0 status=0x0 data_ways=0 tag_ways=0 on every plane
        lines = host_request.execute(self.Proxy(0, 0), host_request.parse("mccplanes"))
        self.assertEqual(lines[0].count("enable=0x0,status=0x0,data_ways=0,tag_ways=0"), 4)


class MccGroupsVerb(unittest.TestCase):
    class Proxy:
        def __init__(self):
            self.reads, self.writes = [], []

        def read32(self, address):
            self.reads.append(address)
            plane, off = (address - 0x200000000) // 0x40000, (address - 0x200000000) % 0x40000
            if plane == 2 and off == 0xA400 + 4 * 8:
                return 0x1E          # usage of group 8 on plane 2
            if off == 0xA000 + 4 * 8:
                return 0x08          # quota of group 8
            return 0

        def write32(self, address, value):
            self.writes.append((address, value))

    def test_reads_three_registers_of_32_groups_on_four_planes_and_writes_nothing(self):
        proxy = self.Proxy()
        lines = host_request.execute(proxy, host_request.parse("mccgroups"))
        self.assertEqual(len(proxy.reads), 4 * 32 * 3)
        self.assertEqual(proxy.writes, [])
        self.assertEqual(lines, ["HOST_REQUEST mccgroups p0 grp:quota/usage/cnt none",
                                 "HOST_REQUEST mccgroups p1 grp:quota/usage/cnt none",
                                 "HOST_REQUEST mccgroups p2 grp:quota/usage/cnt 8:8/1e/0",
                                 "HOST_REQUEST mccgroups p3 grp:quota/usage/cnt none"])


if __name__ == "__main__":
    unittest.main()


class MseVerbTest(unittest.TestCase):
    """BAR-fault reproduction verb (EXP2613): one Command register, MSE bit only."""

    class FakeProxy:
        def __init__(self, value):
            self.mem = {}
            self.value = value
            self.writes = []
        def read16(self, addr):
            return self.mem.get(addr, self.value)
        def write16(self, addr, value):
            self.writes.append((addr, value))
            self.mem[addr] = value

    def test_parse_and_bounds(self):
        self.assertEqual(host_request.parse("mse 0x690000000 1 0 0 off"),
                         [("mse", (0x690000000, 1, 0, 0, False))])
        for bad in ("mse 0x690000000 0 0 0 off", "mse 0x690000004 1 0 0 off",
                    "mse 0x690000000 1 32 0 on", "mse 0x690000000 1 0 8 on",
                    "mse 0x690000000 1 0 0 maybe", "mse 0x690000000 1 0 off"):
            with self.assertRaises(ValueError):
                host_request.parse(bad)

    def test_off_clears_only_mse_and_on_restores(self):
        proxy = self.FakeProxy(0x0406)
        reg = 0x690000000 + (1 << 20) + 4
        lines = host_request.execute(proxy, host_request.parse("mse 0x690000000 1 0 0 off"))
        self.assertEqual(proxy.writes, [(reg, 0x0404)])
        self.assertIn("command 0x0406 -> 0x0404", lines[0])
        lines = host_request.execute(proxy, host_request.parse("mse 0x690000000 1 0 0 on"))
        self.assertEqual(proxy.writes[-1], (reg, 0x0406))
        self.assertIn("command 0x0404 -> 0x0406", lines[0])


class ApgpioVerb(unittest.TestCase):
    """R3 count-only AP GPIO filter: `apgpio` asks m1n1 for the access summary (P_HV_APGPIO_STATS 0xc49).

    m1n1 prints the summary line itself and returns the total access count; images built without
    APGPIO_FILTER answer Bad Command, which must not take the launcher's interrupt path down."""

    class Proxy:
        def __init__(self, total=None, error=None):
            self.calls, self.total, self.error = [], total, error

        def request(self, op, *args, **kwargs):
            self.calls.append((op, args))
            if self.error:
                raise self.error
            return self.total

    def test_parse(self):
        self.assertEqual(host_request.parse("apgpio"), [("apgpio", 0)])
        for bad in ("apgpio 1", "apgpio now"):
            with self.assertRaises(ValueError):
                host_request.parse(bad)

    def test_stats_op_is_0xc49_without_arguments(self):
        proxy = self.Proxy(total=4321)
        lines = host_request.execute(proxy, host_request.parse("apgpio"))
        self.assertEqual(proxy.calls, [(0xC49, ())])
        self.assertEqual(lines, ["HOST_REQUEST apgpio total=4321 (summary line: HV: APGPIO stats ...)"])

    def test_image_without_the_filter_reports_unavailable(self):
        class ProxyCommandError(Exception):
            pass
        lines = host_request.execute(self.Proxy(error=ProxyCommandError("Bad Command")),
                                     host_request.parse("apgpio"))
        self.assertEqual(lines, ["HOST_REQUEST apgpio unavailable ProxyCommandError: Bad Command"])


class TrackbVerb(unittest.TestCase):
    """Audio Track B live facts (TRACK_B_HW_FACTS.md table B, a05ce9046): fixed read-only list, H2 first; the
    MCA cluster-1 / port registers (H4, H5) are read only when H2 shows MCA1 active (PS bits 7:4 = 0xf),
    because a read of a gated block from EL2 may raise an SError."""

    class Proxy:
        def __init__(self, mca1):
            self.reads, self.mca1 = [], mca1

        def read32(self, address):
            self.reads.append(address)
            return self.mca1 if address == 0x23B7002C0 else 0x1234

        def write32(self, *a):
            raise AssertionError("trackb must not write")

    H45 = {0x238400600, 0x238400604, 0x238400608, 0x238404600, 0x238404604, 0x238404608,
           0x238404000, 0x238404100, 0x238404300}

    def test_parse(self):
        self.assertEqual(host_request.parse("trackb"), [("trackb", 0)])
        with self.assertRaises(ValueError):
            host_request.parse("trackb 0x238200094")

    def test_h2_first_and_cluster1_read_when_active(self):
        proxy = self.Proxy(0xFF)
        lines = host_request.execute(proxy, host_request.parse("trackb"))
        self.assertEqual(proxy.reads[0], 0x23B7002C0)
        self.assertTrue(self.H45 <= set(proxy.reads))
        self.assertTrue(any("H2" in l and "mca1_active=1" in l for l in lines))
        self.assertTrue(any(l.startswith("HOST_REQUEST trackb H5 0x238404300=0x00001234") for l in lines))

    def test_gated_mca1_skips_h4_h5(self):
        proxy = self.Proxy(0x0F)   # actual state 0: not active
        lines = host_request.execute(proxy, host_request.parse("trackb"))
        self.assertFalse(self.H45 & set(proxy.reads))
        self.assertTrue(any("H4 skipped" in l for l in lines) and any("H5 skipped" in l for l in lines))
        self.assertIn(0x238200094, proxy.reads)   # H1 still read
        self.assertIn(0x23B048000, proxy.reads)   # H6 still read


class ApgpioProbeVerb(unittest.TestCase):
    """L3/B3 deny proof (m1n1 ed8f1677 P_HV_APGPIO_PROBE 0xc4c): host-side guards mirror m1n1's plan - never the
    guest's pin 195, never a value with the data bit (bit 0) set, pins 0..211 only - so a bad request never reaches
    the proxy; the return word is decoded (action | kind << 8 | refused delta << 16 | pin-write delta << 24)."""

    class Proxy:
        def __init__(self, ret=0):
            self.calls, self.ret = [], ret

        def request(self, op, *args, **kwargs):
            self.calls.append((op, args))
            return self.ret

    def test_parse_and_guards(self):
        self.assertEqual(host_request.parse("apgpioprobe 181"), [("apgpioprobe", (181, 0x2))])
        self.assertEqual(host_request.parse("apgpioprobe 181 0x0"), [("apgpioprobe", (181, 0x0))])
        for bad in ("apgpioprobe 195", "apgpioprobe 181 0x3", "apgpioprobe 181 1", "apgpioprobe 212",
                    "apgpioprobe", "apgpioprobe x"):
            with self.assertRaises(ValueError):
                host_request.parse(bad)

    def test_decodes_called_hook_with_one_refusal(self):
        # action 0 called-hook, kind 1 filter-hook, refused delta 1, pin-write delta 0
        proxy = self.Proxy(ret=0 | (1 << 8) | (1 << 16))
        lines = host_request.execute(proxy, host_request.parse("apgpioprobe 181"))
        self.assertEqual(proxy.calls, [(0xC4C, (181, 0x2))])
        self.assertEqual(lines, ["HOST_REQUEST apgpioprobe pin 181 value 0x2 action=called-hook stage2=filter-hook "
                                 "refused_delta=1 pin_write_delta=0 (detail line: HV: APGPIO probe ...)"])
