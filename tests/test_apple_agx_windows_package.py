from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
WINDOWS = ROOT / "drivers" / "apple-agx" / "windows"
WORKFLOW = ROOT / ".github" / "workflows" / "apple-agx-wdk.yml"


class AppleAgxWindowsPackageTests(unittest.TestCase):
    def read(self, relative):
        return (WINDOWS / relative).read_text()

    def test_package_binds_only_to_the_opt_in_acpi_device(self):
        inf = self.read("AppleAgx.inf")
        self.assertIn("Class=Display", inf)
        self.assertIn("ClassGuid={4d36e968-e325-11ce-bfc1-08002be10318}",
                      inf)
        self.assertIn("ACPI\\APPL0002", inf)
        self.assertNotIn("PCI\\CC_03", inf)
        self.assertIn("CatalogFile=AppleAgx.cat", inf)

    def test_driver_uses_full_dxgkrnl_entry_not_display_only(self):
        driver = self.read("src/driver.c")
        self.assertIn("DRIVER_INITIALIZATION_DATA", driver)
        self.assertIn("DxgkInitialize(", driver)
        self.assertNotIn("DxgkInitializeDisplayOnlyDriver", driver)
        self.assertRegex(driver, r"return\s+DxgkInitialize\(")

    def test_wdk_display_headers_follow_required_base_type_order(self):
        header = self.read("include/apple_agx_driver.h")
        ordered = (
            "#include <ntddk.h>",
            "#include <windef.h>",
            "#include <winerror.h>",
            "#include <wingdi.h>",
            "#include <ntddvdeo.h>",
            "#include <d3dkmddi.h>",
            "#include <d3dkmthk.h>",
            "#include <dispmprt.h>",
        )
        positions = [header.index(include) for include in ordered]
        self.assertEqual(positions, sorted(positions))

    def test_skeleton_registers_only_fail_closed_lifecycle_callbacks(self):
        driver = self.read("src/driver.c")
        required = (
            "DxgkDdiAddDevice", "DxgkDdiStartDevice", "DxgkDdiStopDevice",
            "DxgkDdiRemoveDevice", "DxgkDdiDispatchIoRequest",
            "DxgkDdiInterruptRoutine", "DxgkDdiDpcRoutine",
            "DxgkDdiQueryChildRelations", "DxgkDdiQueryChildStatus",
            "DxgkDdiQueryDeviceDescriptor", "DxgkDdiSetPowerState",
            "DxgkDdiResetDevice", "DxgkDdiUnload",
            "DxgkDdiQueryAdapterInfo",
        )
        for callback in required:
            self.assertRegex(driver, rf"initialization\.{callback}\s*=")

        forbidden = (
            "DxgkDdiCreateDevice", "DxgkDdiCreateAllocation",
            "DxgkDdiCreateContext", "DxgkDdiRender",
            "DxgkDdiSubmitCommand", "DxgkDdiSubmitCommandVirtual",
            "DxgkDdiPresent",
        )
        for callback in forbidden:
            self.assertNotRegex(driver, rf"initialization\.{callback}\s*=")

    def test_start_is_read_only_and_advertises_no_output_or_render_node(self):
        adapter = self.read("src/adapter.c")
        self.assertIn("*NumberOfVideoPresentSources = 0", adapter)
        self.assertIn("*NumberOfChildren = 0", adapter)
        self.assertIn("AppleAgxValidateTranslatedResources", adapter)
        self.assertIn("return STATUS_NOT_SUPPORTED", adapter)
        for unsafe in (
            "WRITE_REGISTER", "MmMapIoSpace", "MmMapIoSpaceEx",
            "DxgkCbMapMemory", "AppleAgxStateTakeFirmwareOwnership",
        ):
            self.assertNotIn(unsafe, adapter)

    def test_resource_parser_requires_exact_contract(self):
        resources = self.read("src/resources.c")
        generated = "j313_agx_g2.generated.h"
        self.assertIn(generated, resources)
        self.assertIn("J313_AGX_G2_SGX_MMIO_BASE", resources)
        self.assertIn("J313_AGX_G2_SGX_MMIO_SIZE", resources)
        self.assertIn("J313_AGX_G2_INTERRUPT_ROUTE_COUNT", resources)
        self.assertIn("CmResourceTypeMemory", resources)
        self.assertIn("CmResourceTypeInterrupt", resources)
        self.assertIn("CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE", resources)

    def test_sources_contain_no_gpu_write_path(self):
        sources = "\n".join(path.read_text() for path in
                            sorted((WINDOWS / "src").glob("*.c")))
        for unsafe in (
            "WRITE_REGISTER", "MmMapIoSpace", "MmMapIoSpaceEx",
            "DxgkCbMapMemory", "DxgkCbSynchronizeExecution",
        ):
            self.assertNotIn(unsafe, sources)

    def test_project_is_arm64_wdm_and_packages_inf(self):
        project = self.read("AppleAgx.vcxproj")
        self.assertIn("Debug|ARM64", project)
        self.assertIn("Release|ARM64", project)
        self.assertIn("WindowsKernelModeDriver10.0", project)
        self.assertIn("<DriverType>WDM</DriverType>", project)
        self.assertIn("AppleAgx.inf", project)
        self.assertIn("apple_agx_state.c", project)
        self.assertIn("j313_agx_g2.generated.h", project)

    def test_build_script_and_ci_run_code_analysis(self):
        build = self.read("scripts/build-driver.ps1")
        workflow = WORKFLOW.read_text()
        self.assertIn("AppleAgx.vcxproj", build)
        self.assertIn("/p:Platform=ARM64", build)
        self.assertIn("/p:RunCodeAnalysis=true", build)
        self.assertIn("AppleAgx.vcxproj", workflow)
        self.assertIn("/p:Platform=ARM64", workflow)
        self.assertIn("/p:RunCodeAnalysis=true", workflow)
        self.assertIn("AppleAgx-ARM64-Debug", workflow)


if __name__ == "__main__":
    unittest.main()
