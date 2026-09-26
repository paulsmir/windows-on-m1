from pathlib import Path
from dataclasses import replace
import json
import re
import shutil
import subprocess
import tempfile
import unittest

from tools.generate_j313_agx_abi_admission import AdmissionContract, render_asl_include


CONTRACT = AdmissionContract(
    contract_version=1, platform="J313", acpi_hid="APPL0002",
    source_g2_sha256="0" * 64,
    memory_resources=(("sgx_mmio", 0x204000000, 0x4000000),
                      ("gpu", 0x9FFFB8000, 0x4000),
                      ("handoff", 0x9FFF70000, 0x4000),
                      ("power_broker", 0x300000000, 0x1000)),
    physical_guest_interrupts=tuple(range(880, 889)),
    synthetic_scanout_guest_interrupt=889,
)


class LocalReserveAmlTest(unittest.TestCase):
    def test_generated_include_matches_checked_in_mu_source(self):
        root = Path(__file__).resolve().parents[1]
        config = json.loads((root / "config/j313-agx-abi-admission.json").read_text())
        source = root / "mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgxAbiAdmission.asl.inc"
        contract = replace(CONTRACT, source_g2_sha256=config["source_g2_sha256"])
        self.assertEqual(render_asl_include(contract), source.read_text())

    def test_dynamic_crs_compiles_and_has_versioned_reserve(self):
        rendered = render_asl_include(CONTRACT)
        self.assertIn("Method (_CRS, 0, Serialized)", rendered)
        self.assertIn("CreateQWordField (LOCR, ^LRNG._MIN, MMIN)", rendered)
        self.assertIn('"agx-local-reserve-version", 0x01', rendered)
        iasl = shutil.which("iasl")
        if not iasl:
            self.skipTest("iasl unavailable")
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "agx.asl"
            source.write_text('DefinitionBlock ("", "SSDT", 2, "APPL", "AGXLOCAL", 1)\n{\n' +
                              rendered + '\n}\n')
            result = subprocess.run([iasl, "-tc", str(source)], capture_output=True,
                                    text=True, cwd=directory)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_dynamic_crs_reconstructs_ipa_from_dword_halves(self):
        iasl = shutil.which("iasl")
        acpiexec = shutil.which("acpiexec")
        if not iasl or not acpiexec:
            self.skipTest("iasl/acpiexec unavailable")
        rendered = render_asl_include(CONTRACT)
        region = re.compile(
            r"    OperationRegion \(LREG,.*?(?=    Name \(BAS0,)", re.S
        )
        names = """    Name (LMAG, 0x4C584741)
    Name (LVER, One)
    Name (LVAL, One)
    Name (LIPA, 0xFFFFFFFFE0000000)
    Name (LIPL, 0xE0000000)
    Name (LIPH, 0x8)
    Name (LHPA, 0x8E0000000)
    Name (LHPL, 0xE0000000)
    Name (LHPH, 0x8)
    Name (LBYT, 0x4000000)
    Name (LBYL, 0x4000000)
    Name (LBYH, Zero)
"""
        rendered, count = region.subn(names, rendered)
        self.assertEqual(count, 1)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "agx.asl"
            source.write_text('DefinitionBlock ("", "SSDT", 2, "APPL", "AGXLOCAL", 1)\n{\n' +
                              rendered + '\n}\n')
            build = subprocess.run([iasl, "-tc", str(source)], capture_output=True,
                                   text=True, cwd=directory)
            self.assertEqual(build.returncode, 0, build.stderr)
            execution = subprocess.run(
                [acpiexec, "-b", r"evaluate \AGX0._CRS", str(source.with_suffix(".aml"))],
                capture_output=True, text=True, cwd=directory,
            )
            self.assertIn("returned object", execution.stdout)
            data = bytearray()
            for line in execution.stdout.splitlines():
                match = re.match(r"\s*[0-9A-F]{4}:\s*(.*?)(?:\s*//|$)", line)
                if match:
                    data.extend(int(byte, 16) for byte in
                                re.findall(r"\b[0-9A-F]{2}\b", match.group(1)))
            offset = 0
            for _ in range(4):
                self.assertEqual(data[offset], 0x8A)
                offset += int.from_bytes(data[offset + 1:offset + 3], "little") + 3
            self.assertEqual(data[offset], 0x8A)
            minimum = int.from_bytes(data[offset + 14:offset + 22], "little")
            maximum = int.from_bytes(data[offset + 22:offset + 30], "little")
            self.assertEqual(minimum, 0x8E0000000)
            self.assertEqual(maximum, 0x8E3FFFFFF)


if __name__ == "__main__":
    unittest.main()
