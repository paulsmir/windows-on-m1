from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers" / "apple-input" / "windows"


class AppleInputWindowsPackageTests(unittest.TestCase):
    def read(self, relative):
        path = DRIVER / relative
        self.assertTrue(path.is_file(), f"missing {path}")
        return path.read_text()

    def test_inf_is_arm64_kmdf_vhf_package(self):
        inf = self.read("AppleInput.inf")
        for required in ("ACPI\\APPL0001", "AppleInput", "NTarm64", "KmdfLibraryVersion",
                         "LowerFilters", "Vhf", "appleinput.sys"):
            self.assertIn(required.lower(), inf.lower())
        self.assertNotRegex(inf.lower(), r"ntamd64|ntx86")

    def test_project_lists_generated_contract_and_sources(self):
        project = self.read("AppleInput.vcxproj")
        self.assertIn("ARM64", project)
        for source in ("driver.c", "device.c"):
            self.assertIn(source, project)
        self.assertIn("j313_apple_input.generated.h", project)
        self.assertIn("vhfkm.lib", project.lower())

    def test_driver_parses_resources_but_does_not_write_mmio(self):
        driver = self.read("src/driver.c")
        device = self.read("src/device.c")
        self.assertIn("WdfDriverCreate", driver)
        self.assertIn("AiDeviceParseResources", device)
        self.assertIn("STATUS_DEVICE_CONFIGURATION_ERROR", device)
        self.assertIn("J313_APPLE_INPUT_GUEST_VINTID", device)
        for forbidden in ("WRITE_REGISTER", "MmMapIoSpace", "WdfInterruptCreate"):
            self.assertNotIn(forbidden, device)

    def test_build_and_recovery_scripts_are_location_independent(self):
        build = self.read("scripts/build-driver.ps1")
        install = self.read("scripts/install-driver.ps1")
        uninstall = self.read("scripts/uninstall-driver.ps1")
        self.assertIn("vswhere", build.lower())
        self.assertIn("pnputil", install.lower())
        self.assertIn("testsigning", install.lower())
        self.assertIn("delete-driver", uninstall.lower())
        self.assertIn("verifier /reset", uninstall.lower())
        combined = build + install + uninstall
        self.assertNotRegex(combined, re.compile(r"/Users/|C:\\Users\\pavel", re.I))


if __name__ == "__main__":
    unittest.main()
