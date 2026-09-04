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

    def test_registers_exp214_plus_full_roskmd_display_contract(self):
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
            "DxgkDdiInterruptRoutine", "DxgkDdiDpcRoutine",
            "DxgkDdiControlInterrupt", "DxgkDdiSetPalette",
            "DxgkDdiGetScanLine",
        )
        for callback in required:
            self.assertIn(f"initialization.{callback} =", driver)

        for callback in (
            "DxgkDdiPresentDisplayOnly", "DxgkDdiSystemDisplayEnable",
            "DxgkDdiSystemDisplayWrite",
        ):
            self.assertNotIn(f"initialization.{callback} =", driver)

    def test_palette_scanline_and_present_are_truthful_fail_closed_boundaries(self):
        display = self.read("src/display.c")
        callbacks = self.read("src/callbacks.c")
        header = self.read("include/render_admission.h")

        self.assertIn("DXGKDDI_SETPALETTE AdmissionDdiSetPalette", header)
        self.assertIn("DXGKDDI_GETSCANLINE AdmissionDdiGetScanLine", header)
        for name in ("AdmissionDdiSetPalette", "AdmissionDdiGetScanLine"):
            self.assertIn(name, display)
        scanline = display[display.index("AdmissionDdiGetScanLine("):]
        self.assertIn("RtlZeroMemory", scanline)
        self.assertIn("STATUS_NOT_SUPPORTED", scanline)
        self.assertIn("Interlocked", scanline)
        present = callbacks[
            callbacks.index("AdmissionDdiPresent("):
            callbacks.index("AdmissionDdiResetFromTimeout(")
        ]
        self.assertIn("STATUS_NOT_SUPPORTED", present)
        self.assertNotIn("STATUS_SUCCESS", present)

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

    def test_fixed_panel_child_monitor_pointer_and_visibility_contract(self):
        display = self.read("src/display.c")
        lifecycle = self.read("src/lifecycle.c")

        for callback in (
            "AdmissionDdiQueryChildRelations",
            "AdmissionDdiQueryChildStatus",
            "AdmissionDdiQueryDeviceDescriptor",
            "AdmissionDdiRecommendMonitorModes",
            "AdmissionDdiQueryVidPnHWCapability",
            "AdmissionDdiSetPointerPosition",
            "AdmissionDdiSetPointerShape",
            "AdmissionDdiSetVidPnSourceVisibility",
        ):
            self.assertIn(callback, display)

        self.assertNotIn("AdmissionDdiQueryChildRelations", lifecycle)
        self.assertIn("ChildRelationsSize < 2 * sizeof(DXGK_CHILD_DESCRIPTOR)", display)
        self.assertIn("ChildDeviceType = TypeVideoOutput", display)
        self.assertIn("D3DKMDT_VOT_INTERNAL", display)
        self.assertIn("StatusConnection", display)
        self.assertIn("HotPlug.Connected = TRUE", display)
        self.assertIn("STATUS_GRAPHICS_CHILD_DESCRIPTOR_NOT_SUPPORTED", display)
        self.assertIn("VideoSignalInfo.TotalSize.cx = 2560", display)
        self.assertIn("VideoSignalInfo.TotalSize.cy = 1600", display)
        self.assertIn("D3DKMDT_FREQUENCY_NOTSPECIFIED", display)
        self.assertIn("D3DKMDT_MP_PREFERRED", display)
        self.assertIn("RtlZeroMemory(&VidPnHWCaps->VidPnHWCaps", display)
        self.assertIn("!SetPointerPosition->Flags.Visible", display)
        self.assertIn("SetVidPnSourceVisibility->Visible", display)

        for forbidden in (
            "Zw", "IoOpenDeviceRegistryKey", "READ_REGISTER",
            "WRITE_REGISTER", "RTKit", "UAT", "DxgkCbNotifyInterrupt",
            "KeWait", "KeDelay", "ExAllocate",
        ):
            self.assertNotIn(forbidden, display)

    def test_one_path_vidpn_state_machine_is_complete_and_hardware_inert(self):
        display = self.read("src/display.c")

        for callback in (
            "AdmissionDdiIsSupportedVidPn",
            "AdmissionDdiRecommendFunctionalVidPn",
            "AdmissionDdiEnumVidPnCofuncModality",
            "AdmissionDdiCommitVidPn",
            "AdmissionDdiUpdateActiveVidPnPresentPath",
        ):
            self.assertIn(callback, display)

        for operation in (
            "DxgkCbQueryVidPnInterface",
            "pfnGetTopology",
            "pfnAcquireFirstPathInfo",
            "pfnAcquireNextPathInfo",
            "pfnReleasePathInfo",
            "pfnCreateNewSourceModeSet",
            "pfnCreateNewTargetModeSet",
            "pfnAssignSourceModeSet",
            "pfnAssignTargetModeSet",
            "pfnAcquirePinnedModeInfo",
            "pfnUpdatePathSupportInfo",
        ):
            self.assertIn(operation, display)

        self.assertIn("hDesiredVidPn == 0", display)
        self.assertIn("VidPnSourceId != 0", display)
        self.assertIn("VidPnTargetId != 0", display)
        self.assertIn("D3DKMDT_RMT_GRAPHICS", display)
        self.assertIn("D3DDDIFMT_A8R8G8B8", display)
        self.assertIn("Format.Graphics.Stride = 10240", display)
        self.assertIn("D3DKMDT_VPPS_IDENTITY", display)
        self.assertIn("D3DKMDT_VPPR_IDENTITY", display)
        self.assertIn("STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN", display)

        for forbidden in (
            "MmMapIoSpace", "READ_REGISTER", "WRITE_REGISTER", "RTKit",
            "UAT", "AppleAgx", "DxgkCbNotifyInterrupt", "DxgkCbNotifyDpc",
        ):
            self.assertNotIn(forbidden, display)

    def test_primary_address_boundary_is_nonpaged_nonblocking_and_fail_closed(self):
        display = self.read("src/display.c")
        header = self.read("include/render_admission.h")
        start = display.index("AdmissionDdiSetVidPnSourceAddress(")
        end = display.index(
            "AdmissionDdiStopDeviceAndReleasePostDisplayOwnership(", start
        )
        source_address = display[start:end]

        self.assertIn("volatile LONG SourceAddressStage", header)
        self.assertIn("volatile LONG SourceAddressStatus", header)
        self.assertIn("InterlockedExchange", source_address)
        self.assertIn("SetVidPnSourceAddress->VidPnSourceId == 0", source_address)
        self.assertIn("STATUS_NOT_SUPPORTED", source_address)
        for forbidden in (
            "AdmissionRecord", "Zw", "IoOpenDeviceRegistryKey", "ExAllocate",
            "KeWait", "KeDelay", "READ_REGISTER", "WRITE_REGISTER",
            "DxgkCbNotifyInterrupt", "DxgkCbNotifyDpc",
        ):
            self.assertNotIn(forbidden, source_address)

        release = display[end:]
        self.assertIn("*DisplayInfo = context->PostDisplayInformation", release)
        self.assertIn("AdmissionDdiStopDevice(context)", release)

    def test_interrupt_admission_owns_only_synthetic_broker_status_and_ack(self):
        interrupt = self.read("src/interrupt.c")
        lifecycle = self.read("src/lifecycle.c")
        header = self.read("include/render_admission.h")
        project = self.read("AppleAgxRenderAdmission.vcxproj")

        self.assertIn('<ClCompile Include="src\\interrupt.c"', project)
        for token in (
            "BrokerBase", "InterruptReady", "InterruptIngressEnabled",
            "InterruptCount", "InterruptAckCount", "DpcCount",
        ):
            self.assertIn(token, header)
        self.assertIn("AdmissionInterruptStart", lifecycle)
        self.assertIn("AdmissionInterruptStop", lifecycle)
        self.assertIn("DxgkCbMapMemory", interrupt)
        self.assertIn("DxgkCbUnmapMemory", interrupt)
        self.assertIn("DxgkCbSynchronizeExecution", interrupt)
        self.assertIn("READ_REGISTER_ULONG", interrupt)
        self.assertIn("WRITE_REGISTER_ULONG", interrupt)
        self.assertIn("SCANOUT_IRQ_STATUS_OFFSET", interrupt)
        self.assertIn("SCANOUT_IRQ_ENABLE_OFFSET", interrupt)
        self.assertIn("SCANOUT_IRQ_MASK", interrupt)
        self.assertIn("MessageNumber != 0", interrupt)
        self.assertIn("DXGK_INTERRUPT_CRTC_VSYNC", interrupt)
        self.assertIn("STATUS_NOT_SUPPORTED", interrupt)

        isr_start = interrupt.index("AdmissionDdiInterruptRoutine(")
        dpc_start = interrupt.index("AdmissionDdiDpcRoutine(")
        isr = interrupt[isr_start:dpc_start]
        self.assertIn("return FALSE", isr)
        self.assertIn("return TRUE", isr)
        self.assertNotIn("DxgkCbQueueDpc", isr)
        for forbidden in (
            "AdmissionRecord", "Zw", "IoOpenDeviceRegistryKey", "ExAllocate",
            "KeWait", "KeDelay", "DbgPrint", "RTKit", "UAT",
        ):
            self.assertNotIn(forbidden, isr)

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
