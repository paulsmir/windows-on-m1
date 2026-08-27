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
        project = self.read("AppleAgx.vcxproj")
        self.assertIn("DRIVER_INITIALIZATION_DATA", driver)
        self.assertIn("DxgkInitialize(", driver)
        self.assertNotIn("DxgkInitializeDisplayOnlyDriver", driver)
        self.assertIn("DXGKDDI_INTERFACE_VERSION_WDDM3_0", project)
        self.assertIn(
            "initialization.Version = DXGKDDI_INTERFACE_VERSION_WDDM3_0",
            driver,
        )
        self.assertRegex(driver, r"status\s*=\s*DxgkInitialize\(")
        self.assertRegex(driver, r"return\s+status\s*;")

    def test_driver_entry_is_declared_before_init_section_pragma(self):
        driver = self.read("src/driver.c")
        declaration = driver.index("DRIVER_INITIALIZE DriverEntry;")
        pragma = driver.index("#pragma alloc_text(INIT, DriverEntry)")
        self.assertLess(declaration, pragma)

    def test_power_qualification_persists_driver_entry_boundary(self):
        driver = self.read("src/driver.c")
        diagnostics_path = WINDOWS / "src" / "driver_diagnostics.c"
        project = self.read("AppleAgx.vcxproj")

        self.assertTrue(diagnostics_path.exists())
        diagnostics = diagnostics_path.read_text()
        self.assertIn("AppleAgxRecordDriverEntryBoundary", diagnostics)
        self.assertIn("ZwOpenKey", diagnostics)
        self.assertIn("ZwSetValueKey", diagnostics)
        self.assertIn("Wom1DriverEntryStage", diagnostics)
        self.assertIn("Wom1DxgkInitializeStatus", diagnostics)
        self.assertIn("#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS", driver)
        self.assertIn("AppleAgxRecordDriverEntryBoundary", driver)
        self.assertRegex(driver, r"status\s*=\s*DxgkInitialize\(")
        self.assertRegex(driver, r"return\s+status\s*;")
        self.assertIn("src\\driver_diagnostics.c", project)

    def test_power_qualification_persists_device_lifecycle_boundaries(self):
        diagnostics = self.read("src/driver_diagnostics.c")

        self.assertIn("IoOpenDeviceRegistryKey", diagnostics)
        self.assertIn("PLUGPLAY_REGKEY_DEVICE", diagnostics)
        self.assertIn("KEY_SET_VALUE", diagnostics)
        self.assertIn("Wom1AddDeviceStage", diagnostics)
        self.assertIn("Wom1AddDeviceStatus", diagnostics)
        self.assertIn("Wom1StartDeviceStage", diagnostics)
        self.assertIn("Wom1StartDeviceStatus", diagnostics)
        self.assertIn("AppleAgxRecordAddDeviceBoundary", diagnostics)
        self.assertIn("AppleAgxRecordStartDeviceBoundary", diagnostics)
        self.assertIn("AppleAgxLogStartStage", diagnostics)
        self.assertIn("#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS", diagnostics)
        self.assertNotIn("MmMapIoSpace", diagnostics)
        self.assertNotIn("WRITE_REGISTER", diagnostics)

    def test_add_and_start_callbacks_publish_the_persistent_boundary(self):
        adapter = self.read("src/adapter.c")

        self.assertGreaterEqual(
            adapter.count("AppleAgxRecordAddDeviceBoundary"), 3
        )
        self.assertGreaterEqual(
            adapter.count("AppleAgxRecordStartDeviceBoundary"), 7
        )
        self.assertNotIn("AppleAgxLogStartStage", adapter)
        self.assertIn("AppleAgxAddEntered", adapter)
        self.assertIn("AppleAgxAddReturned", adapter)
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

    def test_power_qualification_persists_bounded_translated_descriptors(self):
        adapter = self.read("src/adapter.c")
        diagnostics = self.read("src/driver_diagnostics.c")
        header = self.read("include/apple_agx_driver.h")

        self.assertIn("AppleAgxRecordTranslatedResources", header)
        self.assertIn("AppleAgxRecordTranslatedResources", diagnostics)
        self.assertIn("APPLE_AGX_DIAGNOSTIC_RESOURCE_LIMIT 16", diagnostics)
        self.assertIn("Wom1ResourceFullCount", diagnostics)
        self.assertIn("Wom1ResourceDescriptorCount", diagnostics)
        self.assertIn("Wom1ResourceOverflow", diagnostics)
        for suffix in (
            "Type", "Share", "Flags", "StartLow", "StartHigh", "Length",
            "Level", "Vector", "AffinityLow", "AffinityHigh",
        ):
            self.assertIn(f'L"Wom1Resource%02lu{suffix}"', diagnostics)
        self.assertIn("#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS", diagnostics)
        self.assertNotIn("MmMapIoSpace", diagnostics)
        self.assertNotIn("WRITE_REGISTER", diagnostics)

        record = adapter.index("AppleAgxRecordTranslatedResources")
        validate = adapter.index("AppleAgxValidateTranslatedResources")
        self.assertLess(record, validate)
        qualification_guard = adapter.rfind(
            "#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS", 0, record
        )
        self.assertGreaterEqual(qualification_guard, 0)
        self.assertLess(record, adapter.index("#endif", qualification_guard))

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

    def test_full_kmd_registers_fail_closed_render_only_contract(self):
        driver = self.read("src/driver.c")
        required = (
            "DxgkDdiAddDevice", "DxgkDdiStartDevice", "DxgkDdiStopDevice",
            "DxgkDdiRemoveDevice", "DxgkDdiDispatchIoRequest",
            "DxgkDdiInterruptRoutine", "DxgkDdiDpcRoutine",
            "DxgkDdiQueryChildRelations", "DxgkDdiQueryChildStatus",
            "DxgkDdiQueryDeviceDescriptor", "DxgkDdiSetPowerState",
            "DxgkDdiResetDevice", "DxgkDdiUnload",
            "DxgkDdiQueryAdapterInfo", "DxgkDdiNotifyAcpiEvent",
            "DxgkDdiQueryInterface", "DxgkDdiControlEtwLogging",
            "DxgkDdiCreateDevice", "DxgkDdiDestroyDevice",
            "DxgkDdiCreateAllocation", "DxgkDdiDestroyAllocation",
            "DxgkDdiDescribeAllocation",
            "DxgkDdiGetStandardAllocationDriverData",
            "DxgkDdiOpenAllocation", "DxgkDdiCloseAllocation",
            "DxgkDdiPatch", "DxgkDdiSubmitCommand",
            "DxgkDdiBuildPagingBuffer", "DxgkDdiPreemptCommand",
            "DxgkDdiRender", "DxgkDdiPresent",
            "DxgkDdiResetFromTimeout", "DxgkDdiRestartFromTimeout",
            "DxgkDdiEscape", "DxgkDdiCollectDbgInfo",
            "DxgkDdiQueryCurrentFence", "DxgkDdiControlInterrupt",
            "DxgkDdiCreateContext", "DxgkDdiDestroyContext",
            "DxgkDdiRenderKm", "DxgkDdiQueryDependentEngineGroup",
            "DxgkDdiQueryEngineStatus", "DxgkDdiResetEngine",
            "DxgkDdiCancelCommand", "DxgkDdiSetPowerComponentFState",
            "DxgkDdiPowerRuntimeControlRequest", "DxgkDdiGetNodeMetadata",
            "DxgkDdiSubmitCommandVirtual", "DxgkDdiCreateProcess",
            "DxgkDdiDestroyProcess", "DxgkDdiCalibrateGpuClock",
            "DxgkDdiSetStablePowerState",
        )
        for callback in required:
            self.assertRegex(driver, rf"initialization\.{callback}\s*=")

        forbidden = ("DxgkDdiPresentDisplayOnly",)
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

    def test_resource_parser_uses_translated_interrupt_semantics(self):
        resources = self.read("src/resources.c")

        self.assertIn("CmResourceTypeDevicePrivate", resources)
        self.assertIn("devicePrivateCount", resources)
        self.assertIn("J313_AGX_G2_MEMORY_RESOURCE_COUNT", resources)
        self.assertIn("descriptor->u.Interrupt.Affinity == 0", resources)
        self.assertIn("AppleAgxRecordTranslatedInterrupt", resources)
        self.assertNotIn("AppleAgxInterruptRoutes", resources)
        self.assertNotIn(".GuestIntId != Vector", resources)
        self.assertNotIn("descriptor->u.DevicePrivate", resources)

    def test_sources_contain_no_gpu_write_path(self):
        sources = "\n".join(
            path.read_text()
            for path in sorted((WINDOWS / "src").glob("*.c"))
            if path.name not in (
                "power.c", "mmio.c", "firmware_transport.c",
                "uat_publication_windows.c",
            )
        )
        for unsafe in (
            "WRITE_REGISTER", "MmMapIoSpace", "MmMapIoSpaceEx",
            "DxgkCbMapMemory", "DxgkCbSynchronizeExecution",
        ):
            self.assertNotIn(unsafe, sources)

    def test_memory_lifecycle_core_is_always_wdk_compiled_but_not_started(self):
        project = self.read("AppleAgx.vcxproj")
        adapter = self.read("src/adapter.c")

        self.assertIn(r"..\shared\src\apple_agx_memory.c", project)
        self.assertIn(r"..\shared\include\apple_agx_memory.h", project)
        self.assertNotIn("AppleAgxMemoryAllocate", adapter)

    def test_uat_memory_owner_is_wdk_compiled_but_disconnected(self):
        project = self.read("AppleAgx.vcxproj")
        adapter = self.read("src/adapter.c")

        self.assertIn(r"..\shared\src\apple_agx_uat.c", project)
        self.assertIn(r"..\shared\src\apple_agx_uat_table.c", project)
        self.assertIn(r"..\shared\src\apple_agx_uat_memory.c", project)
        self.assertIn(r"..\shared\src\apple_agx_initdata.c", project)
        self.assertIn(r"..\shared\src\apple_agx_initdata_memory.c", project)
        self.assertIn(r"..\shared\src\apple_agx_firmware_status.c", project)
        self.assertIn(r"..\shared\src\apple_agx_channel_info.c", project)
        self.assertIn(r"..\shared\src\apple_agx_channel_memory.c", project)
        self.assertIn(r"..\shared\src\apple_agx_regionc.c", project)
        self.assertIn(r"..\shared\src\apple_agx_uat_publication.c", project)
        self.assertIn(r"..\shared\include\apple_agx_uat.h", project)
        self.assertIn(r"..\shared\include\apple_agx_uat_table.h", project)
        self.assertIn(r"..\shared\include\apple_agx_uat_memory.h", project)
        self.assertIn(r"..\shared\include\apple_agx_initdata.h", project)
        self.assertIn(r"..\shared\include\apple_agx_initdata_memory.h", project)
        self.assertIn(r"..\shared\include\apple_agx_firmware_status.h", project)
        self.assertIn(r"..\shared\include\apple_agx_channel_info.h", project)
        self.assertIn(r"..\shared\include\apple_agx_channel_memory.h", project)
        self.assertIn(r"..\shared\include\apple_agx_regionc.h", project)
        self.assertIn(r"..\shared\include\apple_agx_uat_publication.h", project)
        self.assertNotIn("AppleAgxUatMemoryOwner", adapter)
        self.assertNotIn("AppleAgxUatCreateAddressSpace", adapter)
        self.assertNotIn("AppleAgxInitdataMemoryBuild", adapter)
        self.assertNotIn("AppleAgxFirmwareStatusEncodeG13V13_5", adapter)

    def test_wddm3_memory_adapter_uses_adl_device_addresses(self):
        memory_path = WINDOWS / "src" / "memory_windows.c"
        self.assertTrue(memory_path.exists())
        memory = memory_path.read_text()
        project = self.read("AppleAgx.vcxproj")

        self.assertIn(r"src\memory_windows.c", project)
        for callback in (
            "DxgkCbCreatePhysicalMemoryObject",
            "DxgkCbAllocateAdl",
            "DxgkCbMapPhysicalMemory",
            "DxgkCbUnmapPhysicalMemory",
            "DxgkCbFreeAdl",
            "DxgkCbDestroyPhysicalMemoryObject",
        ):
            self.assertIn(callback, memory)
        self.assertIn("RequireContiguous = 1", memory)
        self.assertIn("pAdl->Flags.Contiguous", memory)
        self.assertIn("BasePageNumber", memory)
        self.assertIn("DXGK_ADL *Adl", memory)
        self.assertNotIn("PDXGK_ADL", memory)
        self.assertNotIn("MmGetPhysicalAddress", memory)
        self.assertNotIn("MmAllocateContiguousMemory", memory)
        self.assertNotIn("AppleAgxWindowsMemoryInitialize", self.read("src/adapter.c"))

    def test_uat_publication_transport_is_noncached_and_disconnected(self):
        source_path = WINDOWS / "src" / "uat_publication_windows.c"
        self.assertTrue(source_path.exists())
        source = source_path.read_text()
        project = self.read("AppleAgx.vcxproj")
        adapter = self.read("src/adapter.c")

        self.assertIn(r"src\uat_publication_windows.c", project)
        self.assertIn("AppleAgxWindowsUatPublicationInitialize", source)
        self.assertIn("DxgkCbMapMemory", source)
        self.assertIn("MmNonCached", source)
        self.assertIn("KeMemoryBarrier", source)
        self.assertIn("DxgkCbUnmapMemory", source)
        self.assertNotIn("AppleAgxWindowsUatPublicationInitialize", adapter)

    def test_mmio_qualification_is_opt_in_inert_and_fail_closed(self):
        adapter = self.read("src/adapter.c")
        mmio_path = WINDOWS / "src" / "mmio.c"
        project = self.read("AppleAgx.vcxproj")
        build = self.read("scripts/build-driver.ps1")

        self.assertTrue(mmio_path.exists())
        mmio = mmio_path.read_text()
        self.assertIn("AppleAgxMmioQualification", project)
        self.assertIn("APPLE_AGX_G2_MMIO_QUALIFICATION=1", project)
        self.assertIn(r"src\mmio.c", project)
        self.assertIn(r"..\shared\src\apple_agx_mapping.c", project)
        self.assertIn("#ifdef APPLE_AGX_G2_MMIO_QUALIFICATION", adapter)
        self.assertIn("AppleAgxQualifyMmioMapping", adapter)
        self.assertIn("[switch]$MmioQualification", build)
        self.assertIn(
            "/p:AppleAgxMmioQualification=$mmioQualification", build
        )
        self.assertIn("DxgkCbMapMemory", mmio)
        self.assertIn("DxgkCbUnmapMemory", mmio)
        self.assertNotIn("READ_REGISTER", mmio)
        self.assertNotIn("WRITE_REGISTER", mmio)
        self.assertNotIn("MmMapIoSpace", mmio)
        self.assertIn("*NumberOfVideoPresentSources = 0", adapter)
        self.assertIn("*NumberOfChildren = 0", adapter)
        self.assertIn("return STATUS_NOT_SUPPORTED", adapter)

    def test_mmio_qualification_persists_map_subview_unmap_receipts(self):
        adapter = self.read("src/adapter.c")
        diagnostics = self.read("src/driver_diagnostics.c")
        header = self.read("include/apple_agx_driver.h")

        self.assertIn("AppleAgxRecordMmioQualification", header)
        self.assertIn("AppleAgxRecordMmioQualification", diagnostics)
        for value_name in (
            "Wom1MmioMapStatus",
            "Wom1MmioSubviewStatus",
            "Wom1MmioUnmapStatus",
            "Wom1MmioSgxStartLow",
            "Wom1MmioSgxStartHigh",
            "Wom1MmioSgxLength",
            "Wom1MmioAscOffset",
            "Wom1MmioAscLength",
        ):
            self.assertIn(value_name, diagnostics)
        self.assertIn("defined(APPLE_AGX_G2_MMIO_QUALIFICATION)", diagnostics)
        self.assertNotIn("READ_REGISTER", diagnostics)
        self.assertNotIn("WRITE_REGISTER", diagnostics)
        self.assertNotIn("MmMapIoSpace", diagnostics)

        map_call = adapter.index("AppleAgxQualifyMmioMapping")
        mapped_receipt = adapter.index("AppleAgxMmioMapped", map_call)
        subview_receipt = adapter.index("AppleAgxMmioSubviewValidated",
                                        mapped_receipt)
        unmap_call = adapter.index("AppleAgxReleaseMmioMapping", subview_receipt)
        unmapped_receipt = adapter.index("AppleAgxMmioUnmapped", unmap_call)
        self.assertLess(map_call, mapped_receipt)
        self.assertLess(mapped_receipt, subview_receipt)
        self.assertLess(subview_receipt, unmap_call)
        self.assertLess(unmap_call, unmapped_receipt)

    def test_all_qualification_profiles_share_lifecycle_diagnostics(self):
        header = self.read("include/apple_agx_driver.h")
        driver = self.read("src/driver.c")
        adapter = self.read("src/adapter.c")
        diagnostics = self.read("src/driver_diagnostics.c")

        self.assertIn("APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS", header)
        self.assertIn("defined(APPLE_AGX_G2_POWER_QUALIFICATION)", header)
        self.assertIn("defined(APPLE_AGX_G2_MMIO_QUALIFICATION)", header)
        self.assertIn("defined(APPLE_AGX_G2_LIFECYCLE_QUALIFICATION)", header)
        self.assertIn("defined(APPLE_AGX_G2_FIRMWARE_QUALIFICATION)", header)
        self.assertIn(
            "#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS", driver
        )
        self.assertGreaterEqual(
            adapter.count(
                "#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS"
            ),
            11,
        )
        self.assertIn(
            "#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS",
            diagnostics,
        )
        self.assertIn("#ifdef APPLE_AGX_G2_POWER_QUALIFICATION", adapter)
        self.assertIn("AppleAgxQualifyPowerBroker", adapter)
        self.assertIn("#ifdef APPLE_AGX_G2_MMIO_QUALIFICATION", adapter)
        self.assertIn("AppleAgxQualifyMmioMapping", adapter)

    def test_lifecycle_qualification_is_diagnostics_only(self):
        header = self.read("include/apple_agx_driver.h")
        project = self.read("AppleAgx.vcxproj")
        build = self.read("scripts/build-driver.ps1")
        workflow = WORKFLOW.read_text()

        self.assertIn("AppleAgxLifecycleQualification", project)
        self.assertIn("APPLE_AGX_G2_LIFECYCLE_QUALIFICATION=1", project)
        self.assertIn("[switch]$LifecycleQualification", build)
        self.assertIn(
            "/p:AppleAgxLifecycleQualification=$lifecycleQualification",
            build,
        )
        self.assertIn("name: lifecycle-qualification", workflow)
        self.assertIn("lifecycle_qualification: true", workflow)
        self.assertIn("AppleAgx-ARM64-LifecycleQualification", workflow)
        self.assertIn(
            "/p:AppleAgxLifecycleQualification=${{ matrix.lifecycle_qualification }}",
            workflow,
        )
        self.assertIn("defined(APPLE_AGX_G2_LIFECYCLE_QUALIFICATION)", header)
        self.assertNotIn(
            "#if defined(APPLE_AGX_G2_LIFECYCLE_QUALIFICATION)",
            self.read("src/adapter.c"),
        )

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

    def test_project_compiles_firmware_core_without_reaching_it_from_ddi(self):
        project = self.read("AppleAgx.vcxproj")
        adapter = self.read("src/adapter.c")
        driver = self.read("src/driver.c")

        for source in (
            r"..\shared\src\apple_agx_rtkit.c",
            r"..\shared\src\apple_agx_firmware.c",
        ):
            self.assertIn(source, project)
        for header in (
            r"..\shared\include\apple_agx_rtkit.h",
            r"..\shared\include\apple_agx_firmware.h",
        ):
            self.assertIn(header, project)

        for ddi_source in (adapter, driver):
            self.assertNotIn("AppleAgxFirmwareStart", ddi_source)
            self.assertNotIn("AppleAgxFirmwareRollback", ddi_source)
        self.assertIn("return STATUS_NOT_SUPPORTED", adapter)

    def test_firmware_transport_is_separate_compile_only_profile(self):
        project = self.read("AppleAgx.vcxproj")
        header = self.read("include/apple_agx_driver.h")
        transport = self.read("src/firmware_transport.c")
        adapter = self.read("src/adapter.c")
        build = self.read("scripts/build-driver.ps1")
        workflow = WORKFLOW.read_text()

        self.assertIn("AppleAgxFirmwareQualification", project)
        self.assertIn("APPLE_AGX_G2_FIRMWARE_QUALIFICATION=1", project)
        self.assertIn(r"src\firmware_transport.c", project)
        self.assertIn(r"..\shared\src\apple_agx_asc_transport.c", project)
        self.assertIn(r"..\shared\include\apple_agx_asc_transport.h", project)
        self.assertIn("APPLE_AGX_WINDOWS_ASC_TRANSPORT", header)
        self.assertIn("AppleAgxFirmwareTransportInitialize", header)
        self.assertIn("READ_REGISTER_ULONG", transport)
        self.assertIn("READ_REGISTER_ULONG64", transport)
        self.assertIn("WRITE_REGISTER_ULONG", transport)
        self.assertIn("WRITE_REGISTER_ULONG64", transport)
        self.assertIn("KeDelayExecutionThread", transport)
        self.assertIn("J313_AGX_G2_ASC_MMIO_SIZE", transport)
        self.assertNotIn("AppleAgxFirmwareStart", adapter)
        self.assertIn("[switch]$FirmwareQualification", build)
        self.assertIn(
            "/p:AppleAgxFirmwareQualification=$firmwareQualification", build
        )
        self.assertIn("name: firmware-qualification", workflow)
        self.assertIn("firmware_qualification: true", workflow)
        self.assertIn("AppleAgx-ARM64-FirmwareQualification", workflow)
        self.assertIn(
            "/p:AppleAgxFirmwareQualification=${{ matrix.firmware_qualification }}",
            workflow,
        )

    def test_firmware_qualification_reads_only_asc_cpu_status_and_unmaps(self):
        adapter = self.read("src/adapter.c")
        header = self.read("include/apple_agx_driver.h")
        mmio = self.read("src/mmio.c")
        transport = self.read("src/firmware_transport.c")
        diagnostics = self.read("src/driver_diagnostics.c")

        shared_guard = (
            r"defined\(APPLE_AGX_G2_MMIO_QUALIFICATION\)\s*\|\|\s*\\?\s*"
            r"defined\(APPLE_AGX_G2_FIRMWARE_QUALIFICATION\)"
        )
        self.assertRegex(header, shared_guard)
        self.assertRegex(mmio, shared_guard)
        self.assertIn("AppleAgxQualifyAscCpuStatus", header)
        self.assertIn("AppleAgxQualifyAscCpuStatus", transport)
        self.assertIn("AppleAgxAscReadCpuStatus", transport)
        self.assertIn("APPLE_AGX_ASC_U32 typedStatus", transport)
        self.assertIn("*CpuStatus = (ULONG)typedStatus", transport)
        self.assertIn("AppleAgxRecordAscCpuStatus", diagnostics)
        for name in ("Wom1AscCpuStatusReadStatus", "Wom1AscCpuStatus"):
            self.assertIn(name, diagnostics)

        start = adapter.index("#ifdef APPLE_AGX_G2_FIRMWARE_QUALIFICATION")
        end = adapter.index("#endif", start)
        qualification = adapter[start:end]
        for required in (
            "AppleAgxQualifyMmioMapping",
            "AppleAgxQualifyAscCpuStatus",
            "AppleAgxRecordAscCpuStatus",
            "AppleAgxReleaseMmioMapping",
        ):
            self.assertIn(required, qualification)
        self.assertLess(
            qualification.index("AppleAgxQualifyMmioMapping"),
            qualification.index("AppleAgxQualifyAscCpuStatus"),
        )
        self.assertLess(
            qualification.index("AppleAgxQualifyAscCpuStatus"),
            qualification.index("AppleAgxReleaseMmioMapping"),
        )
        for forbidden in (
            "AppleAgxAscSetRun",
            "AppleAgxAscSend",
            "AppleAgxAscReceive",
            "AppleAgxFirmwareStart",
            "WRITE_REGISTER",
            "AppleAgxQualifyPowerBroker",
        ):
            self.assertNotIn(forbidden, qualification)

    def test_rtkit_ready_stop_is_a_separate_opt_in_profile(self):
        project = self.read("AppleAgx.vcxproj")
        header = self.read("include/apple_agx_driver.h")
        transport = self.read("src/firmware_transport.c")
        adapter = self.read("src/adapter.c")
        build = self.read("scripts/build-driver.ps1")
        workflow = WORKFLOW.read_text()

        self.assertIn("AppleAgxRtkitQualification", project)
        self.assertIn("APPLE_AGX_G2_RTKIT_QUALIFICATION=1", project)
        self.assertIn("AppleAgxQualifyRtkitReadyStop", header)
        self.assertIn("AppleAgxRtkitSessionBoot", transport)
        self.assertIn("AppleAgxRtkitSessionStop", transport)
        mmio_declarations = header[
            header.rfind("#if", 0, header.index("AppleAgxQualifyMmioMapping")):
            header.index("AppleAgxReleaseMmioMapping")
        ]
        self.assertIn("APPLE_AGX_G2_RTKIT_QUALIFICATION", mmio_declarations)
        self.assertIn("#ifdef APPLE_AGX_G2_RTKIT_QUALIFICATION", adapter)
        start = adapter.index("#ifdef APPLE_AGX_G2_RTKIT_QUALIFICATION")
        end = adapter.index("#endif", start)
        qualification = adapter[start:end]
        for required in (
            "AppleAgxQualifyMmioMapping",
            "AppleAgxPowerSessionBegin",
            "AppleAgxQualifyRtkitReadyStop",
            "AppleAgxPowerSessionEnd",
            "AppleAgxReleaseMmioMapping",
        ):
            self.assertIn(required, qualification)
        for forbidden in (
            "AppleAgxWindowsMemoryInitialize",
            "AppleAgxWindowsUatPublicationInitialize",
            "AppleAgxFirmwareStart",
            "AppleAgxDdiSubmitCommand",
            "AppleAgxDdiPresent",
        ):
            self.assertNotIn(forbidden, qualification)
        self.assertIn("[switch]$RtkitQualification", build)
        self.assertIn("name: rtkit-qualification", workflow)
        self.assertIn("AppleAgx-ARM64-RtkitQualification", workflow)

    def test_powered_status_qualification_brackets_one_read_and_cleans_up(self):
        adapter = self.read("src/adapter.c")
        header = self.read("include/apple_agx_driver.h")
        power = self.read("src/power.c")
        diagnostics = self.read("src/driver_diagnostics.c")
        project = self.read("AppleAgx.vcxproj")
        build = self.read("scripts/build-driver.ps1")
        workflow = WORKFLOW.read_text()

        self.assertIn("APPLE_AGX_G2_POWERED_STATUS_QUALIFICATION", header)
        self.assertIn("AppleAgxPoweredStatusQualification", project)
        self.assertIn("[switch]$PoweredStatusQualification", build)
        self.assertIn("name: powered-status-qualification", workflow)
        self.assertIn("AppleAgx-ARM64-PoweredStatusQualification", workflow)
        self.assertIn("AppleAgxPowerSessionBegin", power)
        self.assertIn("AppleAgxPowerSessionEnd", power)
        self.assertIn("AppleAgxPowerAcquire", power)
        self.assertIn("AppleAgxPowerRelease", power)
        self.assertIn("AppleAgxRecordPowerSession", header)
        self.assertIn("AppleAgxRecordPowerSession", diagnostics)
        for name in (
            "Wom1PowerAcquireStatus",
            "Wom1PowerReleaseStatus",
        ):
            self.assertIn(name, diagnostics)
        self.assertEqual(
            adapter.count("PHYSICAL_ADDRESS powerBrokerAddress = {0};"), 3
        )

        start = adapter.index("#ifdef APPLE_AGX_G2_POWERED_STATUS_QUALIFICATION")
        end = adapter.index("#endif", start)
        qualification = adapter[start:end]
        for required in (
            "AppleAgxQualifyMmioMapping",
            "AppleAgxPowerSessionBegin",
            "AppleAgxRecordPowerSession",
            "AppleAgxQualifyAscCpuStatus",
            "AppleAgxPowerSessionEnd",
            "AppleAgxReleaseMmioMapping",
        ):
            self.assertIn(required, qualification)
        self.assertLess(qualification.index("AppleAgxQualifyMmioMapping"),
                        qualification.index("AppleAgxPowerSessionBegin"))
        self.assertLess(qualification.index("AppleAgxPowerSessionBegin"),
                        qualification.index("AppleAgxRecordPowerSession"))
        self.assertLess(qualification.index("AppleAgxRecordPowerSession"),
                        qualification.index("AppleAgxQualifyAscCpuStatus"))
        self.assertLess(qualification.index("AppleAgxQualifyAscCpuStatus"),
                        qualification.index("AppleAgxPowerSessionEnd"))
        self.assertLess(qualification.index("AppleAgxPowerSessionEnd"),
                        qualification.index("AppleAgxReleaseMmioMapping"))
        self.assertEqual(qualification.count("AppleAgxRecordPowerSession"), 2)
        self.assertEqual(qualification.count("AppleAgxQualifyAscCpuStatus"), 1)
        for forbidden in (
            "AppleAgxAscSetRun",
            "AppleAgxAscSend",
            "AppleAgxAscReceive",
            "AppleAgxFirmwareStart",
            "AppleAgxDdiInterruptRoutine",
            "AppleAgxDdiCreateAllocation",
            "AppleAgxDdiSubmitCommand",
            "AppleAgxDdiRender",
            "AppleAgxDdiPresent",
        ):
            self.assertNotIn(forbidden, qualification)

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

    def test_ci_publishes_separate_qualification_packages(self):
        workflow = WORKFLOW.read_text()
        self.assertIn("qualification: false", workflow)
        self.assertIn("qualification: true", workflow)
        self.assertGreaterEqual(workflow.count("mmio_qualification: false"), 2)
        self.assertIn("name: mmio-qualification", workflow)
        self.assertIn("mmio_qualification: true", workflow)
        self.assertIn("AppleAgx-ARM64-Debug", workflow)
        self.assertIn("AppleAgx-ARM64-PowerQualification", workflow)
        self.assertIn("AppleAgx-ARM64-MmioQualification", workflow)
        self.assertIn("AppleAgx-ARM64-LifecycleQualification", workflow)
        self.assertIn(
            "/p:AppleAgxPowerQualification=${{ matrix.qualification }}",
            workflow,
        )
        self.assertIn(
            "/p:AppleAgxMmioQualification=${{ matrix.mmio_qualification }}",
            workflow,
        )

    def test_ci_verifies_and_publishes_the_wdk_test_signature(self):
        workflow = WORKFLOW.read_text()
        self.assertIn("if: matrix.qualification", workflow)
        self.assertIn("timeout-minutes: 2", workflow)
        self.assertIn("Get-AuthenticodeSignature", workflow)
        self.assertIn("Get-PfxCertificate", workflow)
        self.assertIn("AppleAgx.cer", workflow)
        self.assertIn("AppleAgx-signature.json", workflow)
        self.assertIn("SignerCertificate.Thumbprint", workflow)
        self.assertNotIn("New-SelfSignedCertificate", workflow)
        self.assertNotIn("sign /fd SHA256", workflow)

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

    def test_lifecycle_cycle_is_hash_pinned_and_device_scoped(self):
        cycle = self.read("scripts/cycle-lifecycle-driver.ps1")
        for parameter in (
            "PackageRoot",
            "EvidenceRoot",
            "ExpectedSysSha256",
            "ExpectedInfSha256",
            "ExpectedCatSha256",
            "ExpectedSignerThumbprint",
        ):
            self.assertIn(parameter, cycle)
        self.assertIn("ACPI\\APPL0002\\0", cycle)
        self.assertIn("Get-FileHash", cycle)
        self.assertIn("Get-AuthenticodeSignature", cycle)
        self.assertIn("& pnputil", cycle)
        self.assertIn('@("/add-driver", $inf, "/install")', cycle)
        self.assertIn("259", cycle)
        self.assertIn("already the exact installed package", cycle)
        self.assertIn("/install", cycle.lower())
        self.assertIn('@("/restart-device", $deviceId)', cycle)
        self.assertIn('@("/scan-devices")', cycle)
        self.assertIn("Wom1", cycle)
        self.assertIn("Event 129", cycle)
        self.assertIn("ConvertTo-Json", cycle)
        self.assertNotIn("/force", cycle.lower())


if __name__ == "__main__":
    unittest.main()
