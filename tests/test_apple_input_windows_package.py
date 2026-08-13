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
        for source in ("driver.c", "device.c", "apple_input_hw.c", "spi.c", "gpio.c"):
            self.assertIn(source, project)
        for source in (
            "apple_spi_plan.c",
            "apple_spihid_crc.c",
            "apple_spihid_discovery.c",
            "apple_spihid_packet.c",
            "apple_spihid_reassembly.c",
            "apple_spihid_transport.c",
        ):
            self.assertIn(source, project)
        self.assertIn("j313_apple_input.generated.h", project)
        self.assertIn("vhfkm.lib", project.lower())

    def test_driver_maps_validated_resources_but_does_not_write_mmio(self):
        driver = self.read("src/driver.c")
        device = self.read("src/device.c")
        spi = self.read("src/spi.c")
        gpio = self.read("src/gpio.c")
        self.assertIn("WdfDriverCreate", driver)
        self.assertIn("AiDeviceParseResources", device)
        self.assertIn("STATUS_DEVICE_CONFIGURATION_ERROR", device)
        self.assertIn("J313_APPLE_INPUT_GUEST_VINTID", device)
        self.assertIn("MmMapIoSpaceEx", device)
        self.assertIn("MmUnmapIoSpace", device)
        self.assertIn("READ_REGISTER", spi)
        self.assertIn("READ_REGISTER", gpio)
        combined = device + spi + gpio
        for forbidden in ("WRITE_REGISTER", "WdfInterruptCreate"):
            self.assertNotIn(forbidden, combined)

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

    def test_build_script_resolves_project_from_windows_directory(self):
        build = self.read("scripts/build-driver.ps1")
        self.assertIn('$root = Split-Path -Parent $PSScriptRoot', build)
        self.assertNotIn(
            '$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)',
            build,
        )
        self.assertIn('(Join-Path $root "AppleInput.vcxproj")', build)

    def test_official_wdk_arm64_ci_is_pinned_and_publishes_package(self):
        packages = (ROOT / "packages.config").read_text(encoding="utf-8")
        props = (ROOT / "Directory.Build.props").read_text(encoding="utf-8")
        workflow = (ROOT / ".github" / "workflows" / "apple-input-wdk.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("Microsoft.Windows.WDK.x64", packages)
        self.assertIn("Microsoft.Windows.WDK.arm64", packages)
        self.assertIn("Microsoft.Windows.SDK.CPP.arm64", packages)
        self.assertIn("Microsoft.Windows.WDK.arm64.props", props)
        self.assertIn("nuget restore", workflow.lower())
        self.assertIn("Get-ChildItem", workflow)
        self.assertIn("stampinf.exe", workflow.lower())
        self.assertIn("GITHUB_PATH", workflow)
        self.assertIn("AppleInput.vcxproj", workflow)
        self.assertIn("Platform=ARM64", workflow)
        self.assertIn("actions/upload-artifact", workflow)


if __name__ == "__main__":
    unittest.main()
