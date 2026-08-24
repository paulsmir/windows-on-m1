import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from extra.check_j313_madt_topology import (
    enabled_gicc_uids,
    gicc_efficiency_classes,
)


MADT_FRAGMENT = """
EFI_ACPI_6_3_GICC_STRUCTURE_INIT( // Icestorm-0
    0, 0, GET_MPIDR(0, 0, 0, 0), EFI_ACPI_6_3_GIC_ENABLED, 50, 0,
    0, 0, 25, 0, 0, 0),
EFI_ACPI_6_3_GICC_STRUCTURE_INIT( // Icestorm-1
    1, 1, GET_MPIDR(0, 0, 0, 1), 0 /* EFI_ACPI_6_3_GIC_ENABLED is intentionally absent */, 54, 0,
    0, 0, 25, 0, 0, 0),
EFI_ACPI_6_3_GICC_STRUCTURE_INIT( // Icestorm-2
    2, 2, GET_MPIDR(0, 0, 0, 2), EFI_ACPI_6_3_GIC_ENABLED /* diagnostic note */, 54, 0,
    0, 0, 25, 0, 0, 0),
"""


ROOT = Path(__file__).resolve().parents[1]
PUBLIC_MADT = (
    ROOT / "mu" / "Silicon" / "Apple" / "T810XFamilyPkg" /
    "AcpiTables" / "MADT_Static.aslc"
)


class TestJ313MadtTopology(unittest.TestCase):
    def test_cpu_stability_experiment_exposes_four_efficiency_and_one_performance_core(self):
        source = PUBLIC_MADT.read_text()

        self.assertEqual(enabled_gicc_uids(source), [0, 1, 2, 3, 4])

        classes = gicc_efficiency_classes(source)
        self.assertEqual([classes[uid] for uid in range(4)], [0, 0, 0, 0])
        self.assertEqual(classes[4], 1)

    def test_returns_only_enabled_gicc_uids_in_source_order(self):
        self.assertEqual(enabled_gicc_uids(MADT_FRAGMENT), [0, 2])

    def test_returns_efficiency_class_by_gicc_uid(self):
        heterogeneous_fragment = MADT_FRAGMENT.replace(
            "0, 0, 25, 0, 0, 0),\nEFI_ACPI_6_3_GICC_STRUCTURE_INIT( // Icestorm-2",
            "0, 0, 25, 0, 1, 0),\nEFI_ACPI_6_3_GICC_STRUCTURE_INIT( // Icestorm-2",
        )
        self.assertEqual(
            gicc_efficiency_classes(heterogeneous_fragment),
            {0: 0, 1: 1, 2: 0},
        )

    def test_cli_rejects_an_incorrect_expected_topology(self):
        root = Path(__file__).resolve().parent.parent
        script = root / "extra" / "check_j313_madt_topology.py"

        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "MADT_Static.aslc"
            source.write_text(MADT_FRAGMENT)
            result = subprocess.run(
                [sys.executable, str(script), str(source), "--expect", "0,1,2"],
                text=True,
                capture_output=True,
                check=False,
            )

        self.assertEqual(result.returncode, 1)
        self.assertIn("observed enabled GICC UIDs: [0, 2]", result.stdout)
        self.assertIn("expected enabled GICC UIDs: [0, 1, 2]", result.stderr)


if __name__ == "__main__":
    unittest.main()
