from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
ADMISSION = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxRenderAdmissionTests(unittest.TestCase):
    def read(self, relative):
        return (ADMISSION / relative).read_text()

    def test_is_a_separate_full_render_wddm3_driver(self):
        driver = self.read("src/driver.c")
        project = self.read("AppleAgxRenderAdmission.vcxproj")

        self.assertIn("DRIVER_INITIALIZATION_DATA", driver)
        self.assertIn("DxgkInitialize(", driver)
        self.assertIn("DXGKDDI_INTERFACE_VERSION_WDDM3_0", driver)
        self.assertIn("C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 1296)",
                      driver)
        self.assertNotIn("KMDDOD_INITIALIZATION_DATA", driver)
        self.assertNotIn("DxgkInitializeDisplayOnlyDriver", driver)
        self.assertIn(
            "DXGKDDI_INTERFACE_VERSION=DXGKDDI_INTERFACE_VERSION_WDDM3_0",
            project,
        )

    def test_registers_full_graphics_display_contract_without_irq_or_kmdod_callbacks(self):
        driver = self.read("src/driver.c")
        required = (
            "DxgkDdiAddDevice", "DxgkDdiStartDevice",
            "DxgkDdiStopDevice", "DxgkDdiRemoveDevice",
            "DxgkDdiQueryAdapterInfo", "DxgkDdiCreateDevice",
            "DxgkDdiCreateAllocation", "DxgkDdiBuildPagingBuffer",
            "DxgkDdiSubmitCommand", "DxgkDdiRender",
            "DxgkDdiPresent", "DxgkDdiCreateContext",
            "DxgkDdiSetPointerPosition", "DxgkDdiSetPointerShape",
            "DxgkDdiIsSupportedVidPn", "DxgkDdiRecommendFunctionalVidPn",
            "DxgkDdiEnumVidPnCofuncModality",
            "DxgkDdiSetVidPnSourceVisibility", "DxgkDdiCommitVidPn",
            "DxgkDdiUpdateActiveVidPnPresentPath",
            "DxgkDdiRecommendMonitorModes", "DxgkDdiQueryVidPnHWCapability",
            "DxgkDdiSetVidPnSourceAddress",
            "DxgkDdiStopDeviceAndReleasePostDisplayOwnership",
        )
        for callback in required:
            self.assertIn(f"initialization.{callback} =", driver)

        for callback in (
            "DxgkDdiInterruptRoutine", "DxgkDdiDpcRoutine",
            "DxgkDdiControlInterrupt", "DxgkDdiGetScanLine",
            "DxgkDdiPresentDisplayOnly",
            "DxgkDdiSystemDisplayEnable", "DxgkDdiSystemDisplayWrite",
        ):
            self.assertNotIn(f"initialization.{callback} =", driver)

    def test_start_is_natural_hardware_inert_render_admission(self):
        lifecycle = self.read("src/lifecycle.c")
        header = self.read("include/render_admission.h")

        for token in (
            "DXGK_START_INFO StartInfo",
            "DXGKRNL_INTERFACE Interface",
            "DXGK_DEVICE_INFO DeviceInformation",
            "DXGK_DISPLAY_INFORMATION PostDisplayInformation",
            "BOOLEAN Started",
            "BOOLEAN DisplayActive",
            "BOOLEAN SourceVisible",
            "ULONG CommittedWidth",
            "ULONG CommittedHeight",
            "ULONG CommittedStride",
        ):
            self.assertIn(token, header)

        self.assertIn("*NumberOfVideoPresentSources = 1", lifecycle)
        self.assertIn("*NumberOfChildren = 1", lifecycle)
        self.assertIn("DxgkCbGetDeviceInformation", lifecycle)
        self.assertIn("DxgkCbAcquirePostDisplayOwnership", lifecycle)
        self.assertIn("PostDisplayInformation.Width != 2560", lifecycle)
        self.assertIn("PostDisplayInformation.Height != 1600", lifecycle)
        self.assertIn("PostDisplayInformation.Pitch != 10240", lifecycle)
        self.assertIn("AdmissionReceiptStartSucceeded", lifecycle)
        self.assertRegex(lifecycle, r"return\s+STATUS_SUCCESS\s*;")
        for forbidden in (
            "MmMapIoSpace", "WRITE_REGISTER", "READ_REGISTER",
            "RTKit", "UAT", "AppleAgx", "DxgkCbMapMemory",
            "DxgkCbNotifyInterrupt",
        ):
            self.assertNotIn(forbidden, lifecycle)

    def test_query_adapter_info_reports_truthful_wddm30_memory_caps(self):
        lifecycle = self.read("src/lifecycle.c")

        for query in (
            "DXGKQAITYPE_DRIVERCAPS",
            "DXGKQAITYPE_WDDMDEVICECAPS",
            "DXGKQAITYPE_PHYSICAL_MEMORY_CAPS",
            "DXGKQAITYPE_IOMMU_CAPS",
            "DXGKQAITYPE_64BITONLYCAPS",
            "DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION",
        ):
            self.assertIn(f"case {query}:", lifecycle)

        self.assertIn("HighestVisibleAddress.QuadPart = 0xFFFFFFFFFFLL", lifecycle)
        self.assertIn("iommuCaps->Value = 0", lifecycle)
        self.assertIn("SupportsOnly64Bit = 1", lifecycle)
        self.assertIn("caps->SupportNonVGA = TRUE", lifecycle)
        self.assertNotIn("FlipOnVSyncMmIo = TRUE", lifecycle)
        self.assertNotIn("SupportSoftwareDeviceBitmaps", lifecycle)
        self.assertNotIn("PreemptionAware = 1", lifecycle)
        self.assertNotIn("MultiEngineAware = 1", lifecycle)
        self.assertNotIn("MapAperture2Supported = 1", lifecycle)

    def test_package_binds_exactly_appl0002_and_is_removable(self):
        inf = self.read("AppleAgxRenderAdmission.inf")

        self.assertIn("Class=Display", inf)
        self.assertIn("ACPI\\APPL0002", inf)
        self.assertIn("AddService=AppleAgxAdmission", inf)
        self.assertIn("StartType=3", inf)
        self.assertIn("CatalogFile=AppleAgxRenderAdmission.cat", inf)
        self.assertNotIn("PCI\\CC_03", inf)

    def test_package_contains_a_loadable_fail_closed_umd_contract(self):
        inf = self.read("AppleAgxRenderAdmission.inf")
        project = self.read("umd/AppleAgxRenderAdmissionUmd.vcxproj")
        source = self.read("umd/src/umd.c")
        exports = self.read("umd/AppleAgxRenderAdmissionUmd.def")

        self.assertIn("UserModeDriverName", inf)
        self.assertIn("AppleAgxRenderAdmissionUmd.dll", inf)
        self.assertIn("CopyFiles=Admission.KmdCopyFiles,Admission.UmdCopyFiles", inf)
        self.assertIn("Admission.UmdCopyFiles=11", inf)
        self.assertIn("<ConfigurationType>DynamicLibrary</ConfigurationType>", project)
        self.assertIn("OpenAdapter10_2", exports)
        self.assertIn("D3D10DDIARG_OPENADAPTER", source)
        self.assertIn("return E_NOTIMPL", source)

    def test_project_links_only_windows_admission_sources(self):
        project = self.read("AppleAgxRenderAdmission.vcxproj")

        for source in (
            "src\\driver.c", "src\\lifecycle.c", "src\\callbacks.c",
            "src\\receipts.c", "src\\display.c",
        ):
            self.assertIn(f'<ClCompile Include="{source}"', project)
        for forbidden in (
            "m1n1", "shared\\src", "mmio", "power", "rtkit", "uat",
            "firmware", "render_job", "submission", "scheduler",
        ):
            self.assertNotIn(forbidden.lower(), project.lower())

    def test_advertised_node_has_truthful_3d_metadata(self):
        lifecycle = self.read("src/lifecycle.c")
        callbacks = self.read("src/callbacks.c")
        header = self.read("include/render_admission.h")

        self.assertIn("NbAsymetricProcessingNodes = 1", lifecycle)
        self.assertIn("NodeOrdinal != 0", callbacks)
        self.assertIn("DXGK_ENGINE_TYPE_3D", callbacks)
        self.assertIn("AdmissionReceiptNodeMetadata", callbacks)
        self.assertIn("return STATUS_SUCCESS", callbacks)
        self.assertIn("AdmissionReceiptNodeMetadata", header)

    def test_reserved_presentation_caps_remain_zero(self):
        lifecycle = self.read("src/lifecycle.c")

        self.assertNotIn("SupportSoftwareDeviceBitmaps", lifecycle)


if __name__ == "__main__":
    unittest.main()
