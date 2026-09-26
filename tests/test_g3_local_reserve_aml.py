from pathlib import Path
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


if __name__ == "__main__":
    unittest.main()
