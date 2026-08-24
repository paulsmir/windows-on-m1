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

    def c_function_body(self, source, name):
        match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", source, re.S)
        self.assertIsNotNone(match, f"missing C function {name}")
        depth = 1
        cursor = match.end()
        while cursor < len(source) and depth:
            if source[cursor] == "{":
                depth += 1
            elif source[cursor] == "}":
                depth -= 1
            cursor += 1
        self.assertEqual(depth, 0, f"unterminated C function {name}")
        return source[match.end():cursor - 1]

    def test_inf_is_arm64_kmdf_vhf_package(self):
        inf = self.read("AppleInput.inf")
        for required in ("ACPI\\APPL0001", "AppleInput", "NTarm64", "KmdfLibraryVersion",
                         "LowerFilters", "Vhf", "appleinput.sys"):
            self.assertIn(required.lower(), inf.lower())
        self.assertNotRegex(inf.lower(), r"ntamd64|ntx86")
        self.assertIn("NTarm64.10.0...22000", inf)

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
        self.assertIn('<FilesToPackage Include="$(TargetPath)"', project)

    def test_driver_maps_validated_resources_before_hardware_primitives(self):
        driver = self.read("src/driver.c")
        device = self.read("src/device.c")
        spi = self.read("src/spi.c")
        gpio = self.read("src/gpio.c")
        self.assertIn("WdfDriverCreate", driver)
        self.assertIn("AiDeviceParseResources", device)
        self.assertIn("STATUS_DEVICE_CONFIGURATION_ERROR", device)
        self.assertIn("J313_APPLE_INPUT_GUEST_VINTID", device)
        self.assertIn("static const LONGLONG AiExpectedBases", device)
        self.assertIn("MmMapIoSpaceEx", device)
        self.assertIn("MmUnmapIoSpace", device)
        self.assertIn("READ_REGISTER", spi)
        self.assertIn("READ_REGISTER", gpio)
        self.assertNotIn("WRITE_REGISTER", device)
        self.assertNotIn("WdfInterruptCreate", spi + gpio)

    def test_bounded_spi_and_gpio_primitive_contract(self):
        header = self.read("include/apple_input_device.h")
        hw_header = self.read("include/apple_input_hw.h")
        spi = self.read("src/spi.c")
        gpio = self.read("src/gpio.c")
        for symbol in (
            "AiSpiInitialize",
            "AiSpiTransfer",
            "AiSpiWritePacketReadStatus",
            "AiGpioResetInputController",
            "AiGpioInputAsserted",
            "AiGpioAcknowledge",
        ):
            self.assertIn(symbol, header)
        self.assertIn("AI_SPI_MAX_TRANSFER_BYTES", hw_header)
        self.assertIn("AiSpiDeadlineExpired", spi)
        self.assertIn("WRITE_REGISTER_NOFENCE_ULONG", spi)
        self.assertIn("WRITE_REGISTER_NOFENCE_ULONG", gpio)
        self.assertIn("J313_APPLE_INPUT_AP_GPIO_PIN", gpio)
        self.assertIn("J313_APPLE_INPUT_NUB_GPIO_PIN", gpio)
        self.assertIn("AI_SPI_WRITE_STATUS_DELAY_US", hw_header)
        self.assertIn("AI_SPI_WRITE_STATUS_SIZE", hw_header)
        self.assertIn("AiSpiWritePacketReadStatus", spi)
        self.assertRegex(spi, r"if\s*\(\s*\(!Tx\s*&&\s*!Rx\)")
        self.assertIn("Tx ? (ULONG)Length : 0", spi)
        self.assertRegex(
            self.read("src/transport.c"),
            r"AiSpiTransfer\(context,\s*NULL,\s*context->ReceivePacket",
        )

    def test_irq_contract_uses_raw_gsi_and_keeps_translated_vector(self):
        device = self.read("src/device.c")
        self.assertNotIn("UNREFERENCED_PARAMETER(Raw)", device)
        self.assertIn("WdfCmResourceListGetCount(Raw)", device)
        self.assertIn("WdfCmResourceListGetCount(Translated)", device)
        self.assertIn("raw_resource->u.Interrupt.Vector", device)
        self.assertIn("translated_resource->u.Interrupt.Vector", device)
        self.assertRegex(
            device,
            r"raw_resource->u\.Interrupt\.Vector\s*!=\s*"
            r"\(ULONG\)J313_APPLE_INPUT_GUEST_VINTID",
        )
        self.assertRegex(
            device,
            r"Context->InterruptVector\s*=\s*"
            r"translated_resource->u\.Interrupt\.Vector",
        )
        self.assertNotRegex(
            device,
            r"translated_resource->u\.Interrupt\.Vector\s*!=\s*"
            r"\(ULONG\)J313_APPLE_INPUT_GUEST_VINTID",
        )

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
        self.assertIn(
            '(Join-Path $root "tools/AppleInputDiag/AppleInputDiag.vcxproj")',
            build,
        )

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
        self.assertIn("AppleInputDiag.vcxproj", workflow)
        self.assertIn("Platform=ARM64", workflow)
        self.assertIn("actions/upload-artifact", workflow)

    def test_transport_isr_only_acknowledges_and_queues_passive_work(self):
        project = self.read("AppleInput.vcxproj")
        device = self.read("src/device.c")
        transport = self.read("src/transport.c")
        for source in ("transport.c", "diagnostics.c"):
            self.assertIn(source, project)

        self.assertIn("WDF_INTERRUPT_CONFIG_INIT", device)
        self.assertIn("PassiveHandling = TRUE", device)
        self.assertIn("AutomaticSerialization = FALSE", device)
        self.assertIn("WdfInterruptCreate", device)

        isr = self.c_function_body(transport, "AiInputInterruptIsr")
        for required in (
            "AiGpioInputAsserted",
            "AiGpioAcknowledge",
            "ai_transport_irq",
            "WdfInterruptQueueWorkItemForIsr",
        ):
            self.assertIn(required, isr)
        for forbidden in (
            "AiSpiTransfer",
            "ExAllocatePool",
            "WdfMemoryCreate",
            "KeDelayExecutionThread",
            "KeStallExecutionProcessor",
            "Vhf",
        ):
            self.assertNotIn(forbidden, isr)

    def test_transport_starts_only_after_framework_enables_interrupts(self):
        device = self.read("src/device.c")
        header = self.read("include/apple_input_device.h")
        transport = self.read("src/transport.c")

        self.assertIn("EvtDeviceD0EntryPostInterruptsEnabled", device)
        self.assertIn("EvtDeviceD0ExitPreInterruptsDisabled", device)
        self.assertIn("EVT_WDF_DEVICE_D0_ENTRY_POST_INTERRUPTS_ENABLED", header)
        self.assertIn("EVT_WDF_DEVICE_D0_EXIT_PRE_INTERRUPTS_DISABLED", header)
        d0_entry = self.c_function_body(device, "AppleInputEvtDeviceD0Entry")
        post_enabled = self.c_function_body(
            device, "AppleInputEvtDeviceD0EntryPostInterruptsEnabled"
        )
        pre_disabled = self.c_function_body(
            device, "AppleInputEvtDeviceD0ExitPreInterruptsDisabled"
        )
        self.assertNotIn("AiTransportStart", d0_entry)
        self.assertIn("AiTransportStart", post_enabled)
        self.assertIn("AiTransportStop", pre_disabled)

        start = self.c_function_body(transport, "AiTransportStart")
        self.assertLess(start.index("ai_discovery_start"),
                        start.index("AiGpioResetInputController"))
        self.assertLess(start.index("HardwareStarted = TRUE"),
                        start.index("AiGpioResetInputController"))

    def test_transport_arms_nub_gpio_as_group_zero_level_low_before_reset(self):
        header = self.read("include/apple_input_device.h")
        gpio = self.read("src/gpio.c")
        transport = self.read("src/transport.c")

        self.assertIn("NTSTATUS AiGpioEnableInputInterrupt", header)
        enable = self.c_function_body(gpio, "AiGpioEnableInputInterrupt")
        for required in (
            "J313_APPLE_INPUT_NUB_GPIO_PIN",
            "J313_APPLE_INPUT_IRQ_STARTUP_GROUP",
            "AI_GPIO_MODE_IRQ_LOW",
            "AiGpioIrqMode",
            "READ_REGISTER_NOFENCE_ULONG",
            "WRITE_REGISTER_NOFENCE_ULONG",
            "AI_GPIO_MODE_MASK",
            "AI_GPIO_GROUP_MASK",
            "AI_GPIO_PERIPH_MASK",
            "AI_GPIO_INPUT_ENABLE",
            "AiGpioAcknowledge",
        ):
            self.assertIn(required, enable)

        start = self.c_function_body(transport, "AiTransportStart")
        self.assertIn("AiGpioEnableInputInterrupt", start)
        self.assertLess(start.index("AiGpioEnableInputInterrupt"),
                        start.index("AiGpioResetInputController"))

    def test_transport_worker_has_a_hard_packet_budget_and_protocol_validation(self):
        transport = self.read("src/transport.c")
        self.assertIn("AI_TRANSPORT_MAX_PACKETS_PER_WORKER 32u", transport)
        worker = self.c_function_body(transport, "AiTransportWorker")
        for required in ("AiSpiTransfer", "AiTransportProcessPacket",
                         "ai_transport_worker_complete"):
            self.assertIn(required, worker)
        for required in (
            "ai_packet_decode",
            "ai_reassembler_push",
            "ai_message_decode",
            "ai_discovery_accept_boot",
            "ai_discovery_response_matches",
        ):
            self.assertIn(required, transport)

    def test_transport_worker_consumes_coalesced_irq_before_returning(self):
        transport = self.read("src/transport.c")
        worker = self.c_function_body(transport, "AiTransportWorker")

        self.assertIn("drain_again", worker)
        self.assertGreaterEqual(worker.count("ai_transport_worker_begin"), 2)
        self.assertLess(worker.index("ai_transport_worker_complete"),
                        worker.rindex("ai_transport_worker_begin"))
        self.assertNotIn("A still-active level will retrigger", worker)

    def test_diagnostic_snapshot_is_versioned_bounded_and_contains_no_payload(self):
        ioctl = self.read("include/apple_input_ioctl.h")
        diagnostics = self.read("src/diagnostics.c")
        for field in (
            "InterruptCount",
            "WorkerQueuedCount",
            "WorkerCompletedCount",
            "SpiTransferCount",
            "SpiTimeoutCount",
            "PacketCrcFailureCount",
            "MessageCrcFailureCount",
            "FragmentFailureCount",
            "KeyboardReportCount",
            "TrackpadReportCount",
            "ResetCount",
            "OfflineCount",
        ):
            self.assertIn(field, ioctl)
        self.assertIn("AI_DIAGNOSTIC_SNAPSHOT_VERSION_1", ioctl)
        self.assertIn("AI_PACKET_HEADER_RING_CAPACITY", ioctl)
        self.assertNotRegex(ioctl, r"(?i)(payload|packetdata|rawpacket)\s*\[")
        self.assertIn("WdfRequestRetrieveOutputBuffer", diagnostics)
        self.assertIn("sizeof(AI_DIAGNOSTIC_SNAPSHOT_V1)", diagnostics)
        self.assertIn("STATUS_BUFFER_TOO_SMALL", diagnostics)
        self.assertIn("WdfRequestCompleteWithInformation", diagnostics)

    def test_diagnostic_clients_are_packaged_without_machine_specific_paths(self):
        cli_project = self.read("tools/AppleInputDiag/AppleInputDiag.vcxproj")
        cli = self.read("tools/AppleInputDiag/main.c")
        kd = (ROOT / "tools" / "kd" / "kd_apple_input.py").read_text(
            encoding="utf-8"
        )
        self.assertIn("ARM64", cli_project)
        self.assertNotIn("<PlatformToolset>v143</PlatformToolset>", cli_project)
        self.assertIn("<RuntimeLibrary>MultiThreaded</RuntimeLibrary>", cli_project)
        self.assertIn("IOCTL_AI_GET_SNAPSHOT", cli)
        self.assertIn("--json", cli)
        self.assertIn("try:", kd)
        self.assertIn("finally:", kd)
        self.assertIn("continue", kd.lower())
        self.assertNotRegex(cli + kd, re.compile(r"/Users/|C:\\Users\\pavel", re.I))

    def test_diagnostic_cli_exposes_the_bounded_packet_header_ring(self):
        cli = self.read("tools/AppleInputDiag/main.c")

        self.assertIn("HeaderWriteIndex", cli)
        self.assertIn("AI_PACKET_HEADER_RING_CAPACITY", cli)
        self.assertIn('\\"headers\\"', cli)
        for field in ("Sequence", "Result", "Flags", "Device", "Offset",
                      "Remaining", "Length"):
            self.assertIn(field, cli)


if __name__ == "__main__":
    unittest.main()
