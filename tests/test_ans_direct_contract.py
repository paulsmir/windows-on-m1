"""ANS-DIRECT contract: driver constants, Mu ACPI and repository configs must agree.

Catches: a guest-layout or ADT-derived address changing in one place only. A wrong DMA
window constant would make the controller DMA to the wrong physical page; a wrong ACPI
range would hand Windows the wrong MMIO.
"""
import json
import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "drivers/storage/appleans/common/ans_direct.h"
ASL = ROOT / "mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAnsDirect.asl.inc"
DSDT = ROOT / "mu/Platform/MacBookAirMid2020Pkg/AcpiTables/DSDT.asl"


def header_constants():
    out = {}
    for name, value in re.findall(r"#define (ANSD_\w+) (0x[0-9a-fA-F]+)(?:ULL|u)?\b", HEADER.read_text()):
        out[name] = int(value, 16)
    return out


class AnsDirectContract(unittest.TestCase):
    def setUp(self):
        self.h = header_constants()
        self.ans = json.loads((ROOT / "config/j313-ans.json").read_text())
        self.layout = json.loads((ROOT / "config/j313-guest-layout.json").read_text())

    def test_physical_resources_match_pinned_dt(self):
        res = self.ans["physical_resources_expected_from_pinned_dt"]
        self.assertEqual(self.h["ANSD_CTRL_BASE"], int(res["controller"]["base"], 16))
        self.assertEqual(self.h["ANSD_CTRL_SIZE"], int(res["controller"]["size"], 16))
        self.assertEqual(self.h["ANSD_MBOX_BASE"], int(res["mailbox"]["base"], 16))
        self.assertEqual(self.h["ANSD_MBOX_SIZE"], int(res["mailbox"]["size"], 16))

    def test_dma_window_matches_guest_layout(self):
        L = self.layout
        self.assertEqual(self.h["ANSD_LOW_IPA"], int(L["low_mem_ipa"], 16))
        self.assertEqual(self.h["ANSD_LOW_PA"], int(L["low_mem_pa"], 16))
        self.assertEqual(self.h["ANSD_LOW_SIZE"], int(L["low_mem_size"], 16))
        # Identity RAM starts at the expanded-profile base (run_uefi --guest-ram-profile expanded,
        # expanded_ram_base(u, 0x818000000)); the legacy phys_base lies inside it.
        self.assertEqual(self.h["ANSD_RAM_BASE"], 0x818000000)
        self.assertLessEqual(self.h["ANSD_RAM_BASE"], int(L["phys_base"], 16))
        self.assertEqual(self.h["ANSD_RAM_END"], int(L["ram_end"], 16))
        # Window edges are 1 MiB aligned, so a 4 KiB PRP page never straddles one.
        for key in ("ANSD_LOW_IPA", "ANSD_LOW_PA", "ANSD_LOW_SIZE", "ANSD_RAM_BASE"):
            self.assertEqual(self.h[key] % (1 << 20), 0, key)

    def test_acpi_direct_variant(self):
        text = ASL.read_text()
        res = self.ans["physical_resources_expected_from_pinned_dt"]
        mems = re.findall(r"0, (0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]+), 0, (0x[0-9A-Fa-f]+)\)", text)
        got = [(int(a, 16), int(b, 16), int(n, 16)) for a, b, n in mems]
        want = []
        for key in ("controller", "mailbox"):
            base, size = int(res[key]["base"], 16), int(res[key]["size"], 16)
            want.append((base, base + size - 1, size))
        self.assertEqual(got, want)  # order matters: range 0 controller, range 1 mailbox
        irq = re.findall(r"Interrupt \(ResourceConsumer, Level, ActiveHigh, Exclusive\) \{ (\d+) \}", text)
        self.assertEqual(irq, [str(self.ans["interrupt"]["guest_gsiv"])])
        for needle in ('_HID, "APPL0003"', "_UID, Zero", "_CCA, One"):
            self.assertIn(needle, text)
        dsdt = DSDT.read_text()
        self.assertRegex(dsdt, r"#ifdef J313_ANS_DIRECT_ACPI\s+Include \(\"J313AppleAnsDirect.asl.inc\"\)")

    def test_guest_interrupt_is_unique(self):
        # ANS012-R1: APPL0002 (AGX ABI admission synthetic scanout) and APPL0003 both claimed
        # exclusive GSIV 889 -> PnP resource conflict -> AppleAns never started -> 0x7B.
        gsiv = self.ans["interrupt"]["guest_gsiv"]
        taken = {int(self.layout["nvme_vintid"])}
        admission = json.loads((ROOT / "config/j313-agx-abi-admission.json").read_text())
        taken.add(admission["synthetic_scanout_interrupt"]["guest"])
        taken.update(int(v) for v in re.findall(r'"guest": (\d+)', (ROOT / "config/j313-agx-g2.json").read_text()))
        taken.update(int(v) for v in re.findall(r'"guest_vintid": (\d+)', (ROOT / "config/j313-apple-input.json").read_text()))
        self.assertNotIn(gsiv, taken)
        self.assertTrue(32 <= gsiv <= 1019)  # SPI range of the m1n1 vGIC (GICD_TYPER ITLines 31)
        self.assertIn("{ %d }" % gsiv, ASL.read_text())

    def test_driver_build_includes_direct_sources(self):
        proj = (ROOT / "drivers/storage/appleans/windows/AppleAns.vcxproj").read_text()
        self.assertIn('<ClCompile Include="direct.c" />', proj)
        self.assertIn('<ClCompile Include="..\\common\\ans_direct.c" />', proj)
        self.assertIn("test_direct.c", (ROOT / "drivers/storage/appleans/common/tests/run.sh").read_text())

    def test_portable_suites_pass(self):
        for script in ("drivers/storage/appleans/common/tests/run.sh",
                       "drivers/storage/appleans/windows/tests/run.sh"):
            result = subprocess.run(["sh", str(ROOT / script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
