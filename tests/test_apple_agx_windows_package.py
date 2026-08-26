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

    def test_driver_entry_is_declared_before_init_section_pragma(self):
        driver = self.read("src/driver.c")
        declaration = driver.index("DRIVER_INITIALIZE DriverEntry;")
        pragma = driver.index("#pragma alloc_text(INIT, DriverEntry)")
        self.assertLess(declaration, pragma)

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
        self.assertIn("J313_AGX_G2_POWER_BROKER_BASE", resources)
        self.assertIn("J313_AGX_G2_POWER_BROKER_SIZE", resources)
        self.assertIn("seenSgxMemory", resources)
        self.assertIn("seenPowerBrokerMemory", resources)
        self.assertIn("J313_AGX_G2_INTERRUPT_ROUTE_COUNT", resources)
        self.assertIn("CmResourceTypeMemory", resources)
        self.assertIn("CmResourceTypeInterrupt", resources)
        self.assertIn("CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE", resources)

    def test_resource_parser_requires_exclusive_resource_ownership(self):
        resources = self.read("src/resources.c")
        self.assertGreaterEqual(
            resources.count(
                "descriptor->ShareDisposition != CmResourceShareDeviceExclusive"
            ),
            2,
        )

    def test_sources_contain_no_gpu_write_path(self):
        sources = "\n".join(
            path.read_text()
            for path in sorted((WINDOWS / "src").glob("*.c"))
            if path.name != "power.c"
        )
        for unsafe in (
            "WRITE_REGISTER", "MmMapIoSpace", "MmMapIoSpaceEx",
            "DxgkCbMapMemory", "DxgkCbSynchronizeExecution",
        ):
            self.assertNotIn(unsafe, sources)

    def test_power_qualification_is_opt_in_bounded_and_always_unmapped(self):
        adapter = self.read("src/adapter.c")
        power = self.read("src/power.c")
        project = self.read("AppleAgx.vcxproj")
        build = self.read("scripts/build-driver.ps1")

        self.assertIn("#ifdef APPLE_AGX_G2_POWER_QUALIFICATION", adapter)
        self.assertIn("AppleAgxQualifyPowerBroker", adapter)
        self.assertIn("AppleAgxPowerQualification", project)
        self.assertIn("APPLE_AGX_G2_POWER_QUALIFICATION=1", project)
        self.assertIn("[switch]$PowerQualification", build)
        self.assertIn("/p:AppleAgxPowerQualification=$qualification", build)

        self.assertIn("DxgkCbMapMemory", power)
        self.assertIn("DxgkCbUnmapMemory", power)
        self.assertIn("AppleAgxPowerQualify", power)
        self.assertIn("J313_AGX_G2_POWER_BROKER_SIZE", power)
        self.assertNotIn("J313_AGX_G2_SGX_MMIO_BASE", power)
        self.assertNotIn("MmMapIoSpace", power)
        self.assertLess(power.index("DxgkCbMapMemory"),
                        power.index("AppleAgxPowerQualify"))
        self.assertLess(power.index("AppleAgxPowerQualify"),
                        power.rindex("DxgkCbUnmapMemory"))

    def test_power_qualification_records_fail_closed_start_boundaries(self):
        adapter = self.read("src/adapter.c")
        diagnostics_path = WINDOWS / "src" / "diagnostics.c"
        self.assertTrue(diagnostics_path.exists())
        diagnostics = diagnostics_path.read_text()
        project = self.read("AppleAgx.vcxproj")

        self.assertIn("AppleAgxLogStartStage", diagnostics)
        self.assertIn("IoAllocateErrorLogEntry", diagnostics)
        self.assertIn("IoWriteErrorLogEntry", diagnostics)
        self.assertIn("APPLE_AGX_START_LOG_BASE", diagnostics)
        self.assertNotIn("ZwSetValueKey", diagnostics)
        self.assertNotIn("MmMapIoSpace", diagnostics)
        for stage in (
            "AppleAgxStartEntered",
            "AppleAgxStartDeviceInformation",
            "AppleAgxStartResourcesValidated",
            "AppleAgxStartStateValidated",
            "AppleAgxStartBrokerAddress",
            "AppleAgxStartBrokerTransaction",
            "AppleAgxStartFailClosed",
        ):
            self.assertIn(stage, adapter)
        self.assertIn("src\\diagnostics.c", project)

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

    def test_ci_publishes_separate_default_and_power_qualification_packages(self):
        workflow = WORKFLOW.read_text()
        self.assertIn("qualification: false", workflow)
        self.assertIn("qualification: true", workflow)
        self.assertIn("AppleAgx-ARM64-Debug", workflow)
        self.assertIn("AppleAgx-ARM64-PowerQualification", workflow)
        self.assertIn(
            "/p:AppleAgxPowerQualification=${{ matrix.qualification }}",
            workflow,
        )

    def test_stage_script_never_installs_or_restarts_the_device(self):
        stage = self.read("scripts/stage-driver.ps1")
        self.assertIn("pnputil /add-driver", stage)
        self.assertNotIn("/install", stage.lower())
        self.assertNotIn("/restart-device", stage.lower())
        self.assertNotIn("ACPI\\APPL0002", stage)

    def test_stage_rollback_removes_only_the_recorded_oem_inf(self):
        rollback = self.read("scripts/remove-staged-driver.ps1")
        self.assertIn("[Parameter(Mandatory=$true)][string]$PublishedName", rollback)
        self.assertIn("pnputil /delete-driver $PublishedName", rollback)
        self.assertNotIn("/uninstall", rollback.lower())
        self.assertNotIn("/force", rollback.lower())


if __name__ == "__main__":
    unittest.main()
