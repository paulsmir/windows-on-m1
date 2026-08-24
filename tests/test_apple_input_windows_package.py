from pathlib import Path
import os
import re
import subprocess
import tempfile
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

    def c_byte_array(self, source, name):
        match = re.search(
            rf"\b{name}\s*\[[^\]]*\]\s*=\s*\{{(.*?)\}};",
            source,
            re.S,
        )
        self.assertIsNotNone(match, f"missing C byte array {name}")
        return bytes(int(value, 16) for value in
                     re.findall(r"0x([0-9a-fA-F]{2})", match.group(1)))

    def c_define_uint(self, source, name):
        match = re.search(rf"^#define\s+{name}\s+(\d+)u$", source, re.M)
        self.assertIsNotNone(match, f"missing unsigned C define {name}")
        return int(match.group(1))

    def parse_hid_descriptor(self, descriptor):
        state = {
            "usage_page": 0,
            "report_size": 0,
            "report_count": 0,
            "report_id": 0,
        }
        local_usages = []
        collections = []
        top_level = []
        finger_collections = 0
        usages = []
        report_bits = {}
        cursor = 0

        while cursor < len(descriptor):
            prefix = descriptor[cursor]
            cursor += 1
            self.assertNotEqual(prefix, 0xfe, "long HID items are forbidden")
            size_code = prefix & 3
            size = 4 if size_code == 3 else size_code
            self.assertLessEqual(cursor + size, len(descriptor))
            payload = descriptor[cursor:cursor + size]
            cursor += size
            value = int.from_bytes(payload, "little") if payload else 0
            item_type = (prefix >> 2) & 3
            tag = prefix >> 4

            if item_type == 1:
                if tag == 0:
                    state["usage_page"] = value
                elif tag == 7:
                    state["report_size"] = value
                elif tag == 8:
                    state["report_id"] = value
                elif tag == 9:
                    state["report_count"] = value
            elif item_type == 2 and tag == 0:
                usage = (state["usage_page"], value)
                local_usages.append(usage)
                usages.append(usage)
            elif item_type == 0:
                if tag == 10:
                    usage = local_usages[-1] if local_usages else (0, 0)
                    if not collections:
                        top_level.append(usage)
                    elif (usage == (0x0d, 0x22) and
                          collections[0] == (0x0d, 0x05)):
                        finger_collections += 1
                    collections.append(usage)
                elif tag == 12:
                    self.assertTrue(collections, "collection stack underflow")
                    collections.pop()
                elif tag in (8, 11):
                    kind = "input" if tag == 8 else "feature"
                    key = (kind, state["report_id"])
                    report_bits[key] = report_bits.get(key, 0) + (
                        state["report_size"] * state["report_count"]
                    )
                local_usages = []

        self.assertFalse(collections, "unclosed HID collection")
        return top_level, finger_collections, set(usages), report_bits

    def test_precision_touchpad_descriptor_contract(self):
        header = self.read("include/apple_precision_touchpad_descriptor.h")
        project = self.read("AppleInput.vcxproj")
        portable = (ROOT / "drivers" / "apple-input" / "protocol" /
                    "include" / "apple_trackpad.h").read_text()
        descriptor = self.c_byte_array(
            header, "AiPrecisionTouchpadReportDescriptorTemplate"
        )
        top_level, fingers, usages, report_bits = self.parse_hid_descriptor(
            descriptor
        )

        self.assertEqual(top_level, [(0x0d, 0x05), (0x0d, 0x0e)])
        self.assertEqual(fingers, 5)
        for usage in (
            (0x0d, 0x47), (0x0d, 0x42), (0x0d, 0x51),
            (0x01, 0x30), (0x01, 0x31), (0x0d, 0x56),
            (0x0d, 0x54), (0x09, 0x01), (0x0d, 0x55),
            (0x0d, 0x59), (0xff00, 0xc5), (0x0d, 0x52),
            (0x0d, 0x57), (0x0d, 0x58),
        ):
            self.assertIn(usage, usages)
        self.assertNotIn((0x01, 0x02), top_level)
        self.assertNotIn((0x0d, 0x30), usages)
        self.assertNotIn((0x0d, 0x48), usages)
        self.assertNotIn((0x0d, 0x49), usages)
        self.assertFalse(any(page in (0x0e, 0x20) for page, _ in usages))
        self.assertNotRegex(header.lower(), r"\b(palm|pressure|width|height|haptic)\b")

        report_contracts = {
            ("input", 1): "AI_PTP_INPUT_REPORT_SIZE",
            ("feature", 2): "AI_PTP_CAPABILITIES_REPORT_SIZE",
            ("feature", 3): "AI_PTP_CERTIFICATION_REPORT_SIZE",
            ("feature", 4): "AI_PTP_INPUT_MODE_REPORT_SIZE",
            ("feature", 5): "AI_PTP_SELECTIVE_REPORT_SIZE",
        }
        for report, size_name in report_contracts.items():
            self.assertEqual(
                report_bits[report],
                (self.c_define_uint(portable, size_name) - 1) * 8,
            )
        self.assertEqual(
            set(report_bits),
            set(report_contracts),
        )

        for symbol in (
            "AI_PTP_DESCRIPTOR_SIZE",
            "AiPrecisionTouchpadReportDescriptorTemplate",
            "AiPrecisionTouchpadDescriptorPatch",
            "AI_PTP_X_PHYSICAL_MIN_OFFSETS",
            "AI_PTP_Y_PHYSICAL_MAX_OFFSETS",
        ):
            self.assertIn(symbol, header)
        for source in (
            "apple_precision_touchpad_descriptor.h",
            "apple_trackpad_axis.c",
            "apple_trackpad_frame.c",
            "apple_precision_touchpad.c",
        ):
            self.assertIn(source, project)

    def test_precision_touchpad_descriptor_patch_is_bounded(self):
        source = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "apple_precision_touchpad_descriptor.h"

static uint32_t get_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

int main(void)
{
    uint8_t descriptor[AI_PTP_DESCRIPTOR_SIZE];
    uint8_t untouched[AI_PTP_DESCRIPTOR_SIZE];
    struct ai_trackpad_axis_contract axes = {
        .x = {-1000, 1000, -123456, 654321, 0x00110001u, -2, true},
        .y = {-2000, 2000, -222222, 777777, 0x00110001u, -2, true},
        .valid = true,
    };
    unsigned int index;

    _Static_assert(sizeof(AiPrecisionTouchpadReportDescriptorTemplate) ==
                   AI_PTP_DESCRIPTOR_SIZE, "descriptor size contract");
    memset(descriptor, 0xa5, sizeof(descriptor));
    assert(AiPrecisionTouchpadDescriptorPatch(
        descriptor, sizeof(descriptor), &axes));
    for (index = 0; index < AI_PTP_DESCRIPTOR_PATCH_COUNT; ++index) {
        assert(descriptor[AI_PTP_X_EXPONENT_OFFSETS[index]] == 0x0e);
        assert(descriptor[AI_PTP_Y_EXPONENT_OFFSETS[index]] == 0x0e);
        assert(get_le32(descriptor + AI_PTP_X_UNIT_OFFSETS[index]) ==
               0x00110001u);
        assert(get_le32(descriptor + AI_PTP_Y_UNIT_OFFSETS[index]) ==
               0x00110001u);
        assert(get_le32(descriptor + AI_PTP_X_PHYSICAL_MIN_OFFSETS[index]) ==
               (uint32_t)-123456);
        assert(get_le32(descriptor + AI_PTP_X_PHYSICAL_MAX_OFFSETS[index]) ==
               654321u);
        assert(get_le32(descriptor + AI_PTP_Y_PHYSICAL_MIN_OFFSETS[index]) ==
               (uint32_t)-222222);
        assert(get_le32(descriptor + AI_PTP_Y_PHYSICAL_MAX_OFFSETS[index]) ==
               777777u);
    }

    memset(untouched, 0x3c, sizeof(untouched));
    assert(!AiPrecisionTouchpadDescriptorPatch(
        untouched, sizeof(untouched) - 1, &axes));
    for (index = 0; index < sizeof(untouched); ++index)
        assert(untouched[index] == 0x3c);
    axes.x.unit_exponent = 8;
    assert(!AiPrecisionTouchpadDescriptorPatch(
        untouched, sizeof(untouched), &axes));
    axes.x.unit_exponent = -2;
    axes.y.unit = 0x11u;
    assert(!AiPrecisionTouchpadDescriptorPatch(
        untouched, sizeof(untouched), &axes));
    assert(!AiPrecisionTouchpadDescriptorPatch(NULL, 0, &axes));
    assert(!AiPrecisionTouchpadDescriptorPatch(
        untouched, sizeof(untouched), NULL));
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source_path = Path(tmp) / "descriptor_patch_test.c"
            binary_path = Path(tmp) / "descriptor_patch_test"
            source_path.write_text(source)
            command = [
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(DRIVER / "include"),
                "-I", str(ROOT / "drivers" / "apple-input" /
                           "protocol" / "include"),
                str(source_path), "-o", str(binary_path),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(binary_path)], check=True, cwd=ROOT)

    def test_trackpad_vhf_feature_callbacks_are_bounded_and_transport_free(self):
        source = self.read("src/vhf_trackpad.c")
        get_feature = self.c_function_body(
            source, "AiTrackpadVhfGetFeature"
        )
        set_feature = self.c_function_body(
            source, "AiTrackpadVhfSetFeature"
        )

        for body, portable_handler in (
            (get_feature, "ai_ptp_get_feature"),
            (set_feature, "ai_ptp_set_feature"),
        ):
            self.assertIn(portable_handler, body)
            self.assertEqual(body.count("VhfAsyncOperationComplete"), 1)
            for required in (
                "reportBuffer", "reportBufferLen", "reportId",
                "TrackpadVhf.FeatureLock",
            ):
                self.assertIn(required, body)
            for forbidden in (
                "AiSpi", "AiTransport", "AiGpio", "WdfWaitLock",
                "ExAllocate", "malloc",
            ):
                self.assertNotIn(forbidden, body)

        start = self.c_function_body(source, "AiTrackpadVhfStart")
        for required in (
            "ai_trackpad_axis_contract_parse",
            "AiPrecisionTouchpadDescriptorPatch",
            "VhfClientContext = Context",
            "EvtVhfAsyncOperationGetFeature = AiTrackpadVhfGetFeature",
            "EvtVhfAsyncOperationSetFeature = AiTrackpadVhfSetFeature",
            "VendorID", "ProductID", "VersionNumber", "VhfCreate", "VhfStart",
        ):
            self.assertIn(required, start)
        self.assertIn("AI_TRACKPAD_INIT_READY", start)

    def test_trackpad_vhf_lifecycle_is_independent_and_passive(self):
        header = self.read("include/apple_input_device.h")
        source = self.read("src/vhf_trackpad.c")
        frontend = self.read("src/vhf_frontend.c")
        device = self.read("src/device.c")
        project = self.read("AppleInput.vcxproj")

        for required in (
            "typedef struct _AI_TRACKPAD_VHF_STATE",
            "VHFHANDLE Handle",
            "BOOLEAN Running",
            "KSPIN_LOCK FeatureLock",
            "struct ai_ptp_feature_state Features",
            "UCHAR ReportDescriptor[AI_PTP_DESCRIPTOR_SIZE]",
            "AI_TRACKPAD_VHF_STATE TrackpadVhf",
            "enum AI_VHF_STATE TrackpadVhfState",
            "struct ai_trackpad_axis_contract TrackpadAxisContract",
        ):
            self.assertIn(required, header)
        self.assertIn("src\\vhf_trackpad.c", project)
        self.assertIn("context->TrackpadVhfState = AiVhfAbsent", device)

        submit = self.c_function_body(source, "AiTrackpadVhfSubmit")
        self.assertIn("AI_PTP_INPUT_REPORT_SIZE", submit)
        self.assertIn("AI_PTP_REPORT_INPUT", submit)
        self.assertIn("VhfReadReportSubmit", submit)
        stop = self.c_function_body(source, "AiTrackpadVhfStop")
        self.assertIn("PAGED_CODE", stop)
        self.assertIn("VhfDelete(handle, TRUE)", stop)

        start_manager = self.c_function_body(frontend, "AiVhfFrontendStart")
        self.assertIn("KeyboardVhfState", start_manager)
        self.assertIn("TrackpadVhfState", start_manager)
        self.assertIn("AiKeyboardVhfStart", start_manager)
        self.assertIn("AiTrackpadVhfStart", start_manager)
        self.assertNotIn("return STATUS_DEVICE_NOT_READY", start_manager)
        stop_manager = self.c_function_body(frontend, "AiVhfFrontendStop")
        self.assertLess(
            stop_manager.index("AiTrackpadVhfStop"),
            stop_manager.index("AiKeyboardVhfStop"),
        )
        self.assertIn("ai_ptp_encode_neutral", stop_manager)

    def test_trackpad_delivery_is_passive_ordered_and_fail_closed(self):
        header = self.read("include/apple_input_device.h")
        transport = self.read("src/transport.c")
        isr = self.c_function_body(transport, "AiInputInterruptIsr")
        process = self.c_function_body(
            transport, "AiTransportProcessTrackpadReport"
        )
        packet = self.c_function_body(transport, "AiTransportProcessPacket")

        for field in (
            "struct ai_trackpad_tracker TrackpadTracker",
            "TrackpadScanTime100us",
            "PublishTrackpad",
            "TrackpadPublicationFailed",
        ):
            self.assertIn(field, header)
        for call in (
            "ai_apple_trackpad_decode",
            "ai_trackpad_tracker_update",
            "ai_ptp_encode_input",
            "AiVhfFrontendSubmitTrackpad",
        ):
            self.assertIn(call, process)
        self.assertLess(process.index("ai_apple_trackpad_decode"),
                        process.index("ai_trackpad_tracker_update"))
        self.assertLess(process.index("ai_trackpad_tracker_update"),
                        process.index("ai_ptp_encode_input"))
        self.assertLess(process.index("ai_ptp_encode_input"),
                        process.index("AiVhfFrontendSubmitTrackpad"))
        self.assertIn("AI_TRACKPAD_INIT_READY", process)
        self.assertIn("PublishTrackpad", process)
        self.assertLess(packet.index("ai_message_decode"),
                        packet.index("AiTransportProcessTrackpadReport"))
        self.assertIn("AI_TRACKPAD_VHF_FAILURE_LIMIT", process)
        self.assertIn("AiVhfFrontendStopTrackpad", process)
        self.assertNotIn("Trackpad", isr)
        stop_trackpad = self.c_function_body(
            self.read("src/vhf_frontend.c"),
            "AiVhfFrontendStopTrackpad",
        )
        self.assertIn("AiTrackpadVhfStop", stop_trackpad)
        self.assertNotIn("AiKeyboardVhfStop", stop_trackpad)

    def test_trackpad_publication_gates_default_off_and_uninstall_off(self):
        device = self.read("src/device.c")
        inf = self.read("AppleInput.inf")
        install = self.read("scripts/install-driver.ps1")
        uninstall = self.read("scripts/uninstall-driver.ps1")

        for gate in ("PublishKeyboard", "PublishTrackpad"):
            self.assertIn(gate, device)
            self.assertIn(f"{gate},0x00010001,0", inf)
            self.assertIn(f"[switch]${gate}", install)
            self.assertIn(gate, uninstall)
        self.assertIn("TransportOnly,0x00010001,1", inf)
        capture_inf = self.read("AppleInputCapture.inf")
        self.assertIn("PublishKeyboard,0x00010001,1", capture_inf)
        self.assertIn("PublishTrackpad,0x00010001,0", capture_inf)
        self.assertIn("pnputil /restart-device", install)

    def test_diagnostic_v4_is_scalar_private_and_backwards_compatible(self):
        ioctl = self.read("include/apple_input_ioctl.h")
        diagnostics = self.read("src/diagnostics.c")
        cli = self.read("tools/AppleInputDiag/main.c")
        combined = ioctl + diagnostics + cli

        for field in (
            "AI_DIAGNOSTIC_SNAPSHOT_VERSION_4",
            "TrackpadAxisXValid",
            "TrackpadAxisYValid",
            "TrackpadVhfState",
            "TrackpadReportDecodedCount",
            "TrackpadReportRejectedCount",
            "TrackpadReportSubmittedCount",
            "TrackpadLastRejection",
            "TrackpadActiveCount",
            "TrackpadAdmittedCount",
            "TrackpadSuppressedCount",
            "TrackpadGetFeatureCount",
            "TrackpadSetFeatureCount",
            "TrackpadFeatureLastStatus",
        ):
            self.assertIn(field, combined)
        self.assertIn("sizeof(AI_DIAGNOSTIC_SNAPSHOT_V1)", diagnostics)
        self.assertIn("sizeof(AI_DIAGNOSTIC_SNAPSHOT_V2)", diagnostics)
        self.assertIn("sizeof(AI_DIAGNOSTIC_SNAPSHOT_V3)", diagnostics)
        self.assertIn("sizeof(AI_DIAGNOSTIC_SNAPSHOT_V4)", diagnostics)
        self.assertNotRegex(
            ioctl + cli,
            r"(?i)(trackpadraw|trackpadpayload|trackpadcoordinates|contactx|contacty)\s*\[",
        )

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
            "apple_spihid_trackpad.c",
            "apple_spihid_descriptors.c",
            "apple_hid_contract.c",
            "apple_spihid_transport.c",
        ):
            self.assertIn(source, project)
        self.assertIn("j313_apple_input.generated.h", project)
        self.assertIn("vhfkm.lib", project.lower())
        self.assertIn('<FilesToPackage Include="$(TargetPath)"', project)

    def test_descriptor_store_is_fixed_size_and_has_no_dynamic_allocation(self):
        source = self.read("../protocol/src/apple_spihid_descriptors.c")
        header = self.read("../protocol/include/apple_spihid.h")

        for symbol in (
            "struct ai_descriptor_slot",
            "struct ai_descriptor_store",
            "ai_descriptor_store_reset",
            "ai_descriptor_store_put",
            "ai_descriptor_store_get",
        ):
            self.assertIn(symbol, source + header)
        self.assertIn("bytes[AI_DESCRIPTOR_MAX]", header)
        self.assertNotRegex(source, r"\b(ExAllocatePool|malloc|calloc|realloc)\b")

    def test_hid_contract_parser_is_bounded_and_has_no_dynamic_allocation(self):
        source = self.read("../protocol/src/apple_hid_contract.c")
        header = self.read("../protocol/include/apple_spihid.h")

        for symbol in (
            "AI_HID_REPORT_ID_CAPACITY",
            "struct ai_hid_input_contract",
            "ai_hid_input_contract_parse",
            "ai_hid_input_report_valid",
        ):
            self.assertIn(symbol, source + header)
        self.assertIn("bytes_by_id[AI_HID_REPORT_ID_CAPACITY]", header)
        self.assertNotRegex(source, r"\b(ExAllocatePool|malloc|calloc|realloc)\b")

    def test_discovery_captures_only_bounded_descriptor_metadata(self):
        header = self.read("include/apple_input_device.h")
        ioctl = self.read("include/apple_input_ioctl.h")
        transport = self.read("src/transport.c")
        diagnostics = self.read("src/diagnostics.c")
        cli = self.read("tools/AppleInputDiag/main.c")
        project = self.read("AppleInput.vcxproj")

        for field in (
            "struct ai_descriptor_store Descriptors",
            "struct ai_hid_input_contract KeyboardInputContract",
            "AiCaptureDiscoveryDescriptor",
        ):
            self.assertIn(field, header + transport)
        capture = self.c_function_body(transport, "AiCaptureDiscoveryDescriptor")
        self.assertIn("AI_DISCOVERY_KEYBOARD_DESCRIPTOR", capture)
        self.assertIn("AI_DISCOVERY_TRACKPAD_DESCRIPTOR", capture)
        self.assertIn("ai_descriptor_store_put", capture)
        self.assertIn("ai_hid_input_contract_parse", capture)
        self.assertNotIn("AI_DISCOVERY_READY", capture)
        process = self.c_function_body(transport, "AiTransportProcessPacket")
        self.assertLess(process.index("AiCaptureDiscoveryDescriptor"),
                        process.index("ai_discovery_accept("))
        start = self.c_function_body(transport, "AiTransportStart")
        self.assertLess(start.index("ai_descriptor_store_reset"),
                        start.index("AiGpioResetInputController"))

        for field in (
            "AI_DIAGNOSTIC_SNAPSHOT_VERSION_3",
            "KeyboardDescriptorLength",
            "TrackpadDescriptorLength",
            "KeyboardDescriptorSha256",
            "TrackpadDescriptorSha256",
            "KeyboardContractValid",
            "DescriptorDigestStatus",
        ):
            self.assertIn(field, ioctl + diagnostics + cli)
        self.assertNotRegex(
            ioctl,
            r"(?i)(payload|rawreport|descriptorbytes)\s*\[",
        )
        self.assertIn("BCryptHashData", diagnostics)
        self.assertIn("BCryptFinishHash", diagnostics)
        self.assertIn("Cng.lib", project)
        self.assertLess(
            diagnostics.index('#include "apple_input_device.h"'),
            diagnostics.index("#include <bcrypt.h>"),
            "bcrypt.h requires the kernel/WDK base types included by "
            "apple_input_device.h",
        )
        self.assertIn('printf("%02x"', cli)
        self.assertIn("keyboard_descriptor_sha256", cli)
        self.assertIn("trackpad_descriptor_sha256", cli)
        self.assertNotRegex(cli, r"(?i)(raw_descriptor|raw_report|payload_bytes)")

    def test_keyboard_vhf_frontend_is_explicitly_gated_and_synchronous(self):
        header = self.read("include/apple_input_device.h")
        device = self.read("src/device.c")
        vhf = self.read("src/vhf_keyboard.c")
        frontend = self.read("src/vhf_frontend.c")
        project = self.read("AppleInput.vcxproj")
        inf = self.read("AppleInput.inf")
        install = self.read("scripts/install-driver.ps1")
        uninstall = self.read("scripts/uninstall-driver.ps1")

        for source in ("vhf_keyboard.c", "vhf_frontend.c"):
            self.assertIn(source, project)
        for symbol in (
            "VHF_CONFIG_INIT",
            "VhfCreate",
            "VhfStart",
            "VhfReadReportSubmit",
            "VhfDelete",
        ):
            self.assertIn(symbol, vhf)
        self.assertIn("WdfDeviceWdmGetDeviceObject", vhf)
        self.assertIn("VhfDelete(handle, TRUE)", vhf)
        self.assertNotIn("EvtVhfReadyForNextReadReport", vhf)
        for symbol in (
            "AiVhfFrontendStart",
            "AiVhfFrontendSubmitKeyboard",
            "AiVhfFrontendStop",
            "AiVhfDescriptorsReady",
            "AiVhfStarting",
            "AiVhfRunning",
            "AiVhfStopping",
            "FrontendLock",
        ):
            self.assertIn(symbol, header + frontend)
        self.assertIn("ai_hid_input_report_valid", vhf)
        self.assertIn("HID_XFER_PACKET", vhf)
        self.assertIn("WdfDriverOpenParametersRegistryKey", device)
        self.assertIn("WdfRegistryQueryULong", device)
        self.assertIn("context->TransportOnly = TRUE", device)
        self.assertIn("TransportOnly,0x00010001,1", inf)
        self.assertIn("[switch]$PublishKeyboard", install)
        self.assertIn("TransportOnly", install)
        self.assertIn("TransportOnly", uninstall)
        self.assertIn("pnputil /restart-device", install)

    def test_keyboard_vhf_dispatch_and_teardown_are_ordered_and_private(self):
        ioctl = self.read("include/apple_input_ioctl.h")
        transport = self.read("src/transport.c")
        device = self.read("src/device.c")
        cli = self.read("tools/AppleInputDiag/main.c")

        process = self.c_function_body(transport, "AiTransportProcessPacket")
        self.assertLess(process.index("AiCaptureDiscoveryDescriptor"),
                        process.index("ai_discovery_accept("))
        self.assertLess(process.index("protocol_status == AI_COMPLETE"),
                        process.index("AiVhfFrontendStart"))
        self.assertLess(process.index("wire.flags == AI_PACKET_READ"),
                        process.index("AiVhfFrontendSubmitKeyboard"))
        self.assertLess(process.index("wire.device == 1u"),
                        process.index("AiVhfFrontendSubmitKeyboard"))
        stop = self.c_function_body(transport, "AiTransportStop")
        self.assertLess(stop.index("Stopping = TRUE"),
                        stop.index("AiVhfFrontendStop"))
        release = self.c_function_body(device,
                                       "AppleInputEvtDeviceReleaseHardware")
        self.assertLess(release.index("AiTransportStop"),
                        release.index("MmUnmapIoSpace"))
        isr = self.c_function_body(transport, "AiInputInterruptIsr")
        self.assertNotIn("Vhf", isr)

        for field in (
            "KeyboardVhfState",
            "KeyboardReportAcceptedCount",
            "KeyboardReportRejectedCount",
            "KeyboardReportSubmittedCount",
            "KeyboardVhfSubmissionFailureCount",
            "KeyboardVhfStartFailureCount",
            "KeyboardVhfLastStatus",
        ):
            self.assertIn(field, ioctl)
            self.assertIn(field, cli)
        self.assertNotRegex(
            ioctl + cli,
            r"(?i)(KeyCode|PressedKeys|RawReport|payloadbytes)\s*\[",
        )

    def test_trackpad_multitouch_init_is_serialized_retriable_and_nonfatal(self):
        header = self.read("include/apple_input_device.h")
        device = self.read("src/device.c")
        transport = self.read("src/transport.c")
        ioctl = self.read("include/apple_input_ioctl.h")
        diagnostics = self.read("src/diagnostics.c")
        cli = self.read("tools/AppleInputDiag/main.c")

        for symbol in (
            "TransportLock",
            "TrackpadInitTimer",
            "struct ai_trackpad_init TrackpadInit",
            "EVT_WDF_TIMER AiTrackpadInitTimer",
        ):
            self.assertIn(symbol, header)
        self.assertIn("WdfWaitLockCreate", device)
        self.assertIn("WdfTimerCreate", device)
        self.assertIn("WdfExecutionLevelPassive", device)

        process = self.c_function_body(transport, "AiTransportProcessPacket")
        worker = self.c_function_body(transport, "AiTransportWorker")
        timer = self.c_function_body(transport, "AiTrackpadInitTimer")
        stop = self.c_function_body(transport, "AiTransportStop")
        self.assertIn("ai_trackpad_init_start", process)
        self.assertIn("ai_trackpad_init_response_matches", process)
        self.assertIn("ai_trackpad_init_accept", process)
        self.assertIn("AiTransportSendTrackpadInitRequest", process)
        self.assertIn("WdfWaitLockAcquire(context->TransportLock", worker)
        self.assertIn("ai_trackpad_init_poll", timer)
        self.assertIn("AiTransportSendTrackpadInitRequest", timer)
        self.assertIn("WdfWaitLockAcquire(context->TransportLock", timer)
        self.assertLess(stop.index("WdfTimerStop"),
                        stop.index("WdfWaitLockAcquire"))
        self.assertIn("AiVhfFrontendStart", process)
        self.assertNotRegex(
            process,
            r"AiVhfFrontendStart\(Context\);\s*if\s*\(!NT_SUCCESS\(status\)\)\s*return status;",
        )

        for field in (
            "TrackpadInitPhase",
            "TrackpadInitRetryCount",
            "TrackpadInitAttemptCount",
        ):
            self.assertIn(field, ioctl)
            self.assertIn(field, diagnostics + transport)
            self.assertIn(field, cli)

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

    def test_transport_does_not_require_responses_to_echo_message_ids(self):
        transport = self.read("src/transport.c")
        process_packet = self.c_function_body(transport, "AiTransportProcessPacket")

        self.assertIn("ai_discovery_response_matches", process_packet)
        self.assertNotIn("message.id", process_packet)

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
        self.assertIn("sizeof(AI_DIAGNOSTIC_SNAPSHOT_V3)", diagnostics)
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

    def test_diagnostics_expose_only_last_decoded_message_header(self):
        ioctl = self.read("include/apple_input_ioctl.h")
        diagnostics = self.read("src/diagnostics.c")
        transport = self.read("src/transport.c")
        cli = self.read("tools/AppleInputDiag/main.c")

        self.assertIn("AI_DIAGNOSTIC_SNAPSHOT_VERSION_2", ioctl)
        self.assertIn("AI_DIAGNOSTIC_SNAPSHOT_V2", ioctl)
        for field in ("MessageType", "MessageReport", "MessageDevice",
                      "MessageId", "MessageResponseLength",
                      "MessagePayloadLength"):
            self.assertIn(field, ioctl)
            self.assertIn(field, cli)
        self.assertNotRegex(ioctl, r"(?i)(payload|packetdata|rawpacket)\s*\[")
        self.assertIn("AiDiagnosticsRecordMessage", diagnostics)
        self.assertIn("AiDiagnosticsRecordMessage(Context, &message)", transport)
        self.assertIn("sizeof(AI_DIAGNOSTIC_SNAPSHOT_V3)", diagnostics)

    def test_trackpad_capture_is_a_separate_manual_test_build(self):
        production = self.read("AppleInput.vcxproj")
        capture = self.read("AppleInputCapture.vcxproj")
        capture_inf = self.read("AppleInputCapture.inf")
        workflow = (ROOT / ".github" / "workflows" /
                    "apple-input-wdk.yml").read_text()

        self.assertNotIn("AI_ENABLE_TRACKPAD_CAPTURE", production)
        self.assertNotIn("trackpad_capture.c", production)
        self.assertIn("AI_ENABLE_TRACKPAD_CAPTURE=1", capture)
        self.assertIn("trackpad_capture.c", capture)
        self.assertIn("AppleInputCapture", capture)
        self.assertIn("Wdmsec.lib", capture)
        self.assertIn("AppleInputCapture.sys", capture_inf)
        self.assertIn("DriverVer=08/24/2026,0.1.3.0", capture_inf)
        self.assertIn("TransportOnly,0x00010001,0", capture_inf)
        self.assertIn("workflow_dispatch:", workflow)
        self.assertIn("build-trackpad-capture-arm64:", workflow)
        self.assertIn("if: github.event_name == 'workflow_dispatch'", workflow)
        capture_job = workflow[workflow.index("build-trackpad-capture-arm64:"):]
        self.assertIn("AppleInputCapture.vcxproj", capture_job)
        self.assertNotIn("github.event_name == 'push'", capture_job)

    def test_trackpad_capture_is_admin_only_bounded_and_device_two_only(self):
        header = self.read("include/apple_input_capture.h")
        source = self.read("src/trackpad_capture.c")
        device = self.read("src/device.c")
        transport = self.read("src/transport.c")

        self.assertIn("AI_TRACKPAD_CAPTURE_MAX_REPORTS 16u", header)
        self.assertIn("AI_TRACKPAD_CAPTURE_MAX_REPORT_SIZE 512u", header)
        self.assertIn("FILE_READ_DATA | FILE_WRITE_DATA", header)
        self.assertIn("FILE_AUTOGENERATED_DEVICE_NAME", device)
        self.assertIn("SDDL_DEVOBJ_SYS_ALL_ADM_ALL", device)
        self.assertIn("#if AI_ENABLE_TRACKPAD_CAPTURE", device)
        self.assertIn("wire.device == 2u", transport)
        self.assertIn("AiTrackpadCaptureRecord", transport)
        record = self.c_function_body(source, "AiTrackpadCaptureRecord")
        self.assertIn("Device != 2u", record)
        self.assertIn("Length > AI_TRACKPAD_CAPTURE_MAX_REPORT_SIZE", record)
        self.assertIn("ReportCount >= AI_TRACKPAD_CAPTURE_MAX_REPORTS", record)
        self.assertNotRegex(source, r"\b(ExAllocatePool|malloc|calloc|realloc)\b")

    def test_trackpad_capture_cli_requires_an_explicit_local_output(self):
        cli = self.read("tools/AppleInputCapture/main.c")
        project = self.read("tools/AppleInputCapture/AppleInputCapture.vcxproj")

        self.assertIn("ARM64", project)
        self.assertIn("IOCTL_AI_TRACKPAD_CAPTURE_ARM", cli)
        self.assertIn("IOCTL_AI_TRACKPAD_CAPTURE_READ", cli)
        self.assertIn("IOCTL_AI_TRACKPAD_CAPTURE_CANCEL", cli)
        self.assertIn("GENERIC_READ | GENERIC_WRITE", cli)
        self.assertIn("capture --count", cli)
        self.assertNotRegex(cli, re.compile(r"/Users/|C:\\\\Users\\\\pavel", re.I))

    def test_trackpad_capture_has_a_release_trigger_without_production_capture(self):
        header = self.read("include/apple_input_capture.h")
        portable_header = (
            ROOT
            / "drivers"
            / "apple-input"
            / "protocol"
            / "include"
            / "apple_trackpad.h"
        ).read_text()
        source = self.read("src/trackpad_capture.c")
        cli = self.read("tools/AppleInputCapture/main.c")
        capture_project = self.read("AppleInputCapture.vcxproj")
        production_project = self.read("AppleInput.vcxproj")
        capture_inf = self.read("AppleInputCapture.inf")

        self.assertIn("AI_TRACKPAD_CAPTURE_VERSION 2u", header)
        self.assertIn("AI_TRACKPAD_CAPTURE_TRIGGER_RELEASE", header)
        self.assertIn("#ifdef AI_KERNEL_MODE", portable_header)
        self.assertIn('#include "apple_spihid.h"', portable_header)
        self.assertNotIn("#include <stdint.h>", portable_header)
        self.assertIn("ai_apple_trackpad_release_candidate", source)
        self.assertIn("capture-release --output", cli)
        self.assertIn("apple_trackpad_frame.c", capture_project)
        self.assertNotIn("AI_ENABLE_TRACKPAD_CAPTURE", production_project)
        self.assertIn("DriverVer=08/24/2026,0.1.3.0", capture_inf)


if __name__ == "__main__":
    unittest.main()
