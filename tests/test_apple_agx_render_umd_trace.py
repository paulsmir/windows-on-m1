from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"
DECODER = RENDER / "scripts" / "decode-umd-render-trace.py"


def load_decoder():
    spec = spec_from_file_location("decode_umd_render_trace", DECODER)
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class AppleAgxRenderUmdTraceTests(unittest.TestCase):
    def test_decoder_keeps_two_render_calls_separate(self):
        decoder = load_decoder()
        lines = []
        for call, context, command_hash in (
            (1, 0x1111222233334444, 0xAAAABBBBCCCCDDDD),
            (2, 0x5555666677778888, 0x123456789ABCDEF0),
        ):
            values = {
                1: (2 << 16) | call,
                2: 0,
                3: 0,
                4: 48,
                5: 4096,
                6: 8192,
                7: 2,
                8: 0,
                9: 64,
                10: 0,
                19: 0,
                20: 0,
                21: context & 0xFFFFFFFF,
                22: context >> 32,
                23: command_hash & 0xFFFFFFFF,
                24: command_hash >> 32,
                25: 168,
                26: 1,
                27: 1,
            }
            for field, value in values.items():
                word = (0x5120 << 48) | (field << 32) | value
                lines.append(
                    f"TTY> HV: AGX power receipt seq={word} "
                    "cmd=0 state=3 result=0"
                )
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "host.log"
            path.write_text("\n".join(lines))
            calls = decoder.decode_calls(path)
        self.assertEqual([call["call_sequence"] for call in calls], [1, 2])
        self.assertEqual(calls[1]["context_token"], 0x5555666677778888)
        self.assertEqual(calls[1]["command_hash"], 0x123456789ABCDEF0)
        self.assertEqual(calls[1]["dma_bytes_produced"], 168)
        self.assertEqual(calls[1]["patches_produced"], 1)
        self.assertEqual(calls[1]["prepatched"], 1)

    def test_decoder_preserves_entry_command_guard_and_status(self):
        decoder = load_decoder()
        names = decoder.NAMES
        lines = []
        for field in sorted(names):
            value = 0 if names[field] in {"guard", "status"} else field + 100
            word = (0x5120 << 48) | (field << 32) | value
            lines.append(
                f"TTY> HV: AGX power receipt seq={word} cmd=0 state=3 result=0"
            )
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "host.log"
            path.write_text("\n".join(lines))
            decoded = decoder.decode(path)
        self.assertEqual(decoded["version"], 101)
        self.assertEqual(decoded["command_magic"], 111)
        self.assertEqual(decoded["guard"], 0)
        self.assertEqual(decoded["status"], 0)

    def test_decoder_accepts_pre_copy_guard_but_requires_final_status(self):
        decoder = load_decoder()
        fields = list(range(1, 11)) + [19, 20]
        lines = []
        for field in fields:
            value = 14 if field == 19 else (0xC00000E8 if field == 20 else field)
            word = (0x5120 << 48) | (field << 32) | value
            lines.append(
                f"TTY> HV: AGX power receipt seq={word} cmd=0 state=3 result=0"
            )
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "host.log"
            path.write_text("\n".join(lines))
            decoded = decoder.decode(path)
        self.assertEqual(decoded["guard"], 14)
        self.assertEqual(decoded["status"], 0xC00000E8)
        self.assertNotIn("command_magic", decoded)

    def test_kmd_trace_is_qualification_only_and_guarded(self):
        header = (RENDER / "include" / "render_submit_trace.h").read_text()
        source = (RENDER / "src" / "umd_render_windows.c").read_text()
        context = (RENDER / "include" / "render_admission.h").read_text()
        self.assertIn("ADMISSION_UMD_RENDER_TRACE_TAG", header)
        self.assertIn("AdmissionUmdRenderTraceWord", header)
        self.assertIn("UmdRenderTraceClaimed", context)
        self.assertIn("PagingCorrelationArmed", context)
        self.assertIn("#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)", source)
        self.assertNotIn("&Context->PagingCorrelationArmed", source)
        self.assertNotIn(
            "&Context->UmdRenderTraceClaimed, 1, 0", source
        )
        self.assertIn("AdmissionUmdRenderTraceArm", source)
        arm = source[
            source.index("VOID AdmissionUmdRenderTraceArm("):
            source.index("VOID AdmissionUmdRenderTraceDisarm(")
        ]
        self.assertNotIn("AdmissionUmdRenderTraceCallCount", arm)
        self.assertIn("AdmissionUmdRenderTraceDisarm", source)
        self.assertIn("AdmissionUmdRenderTraceAdapterGet", source)
        correlation = (
            RENDER / "src" / "render_call_correlation_windows.c"
        ).read_text()
        begin = correlation[
            correlation.index("ULONG AdmissionRenderCorrelationBeginWindows("):
            correlation.index("VOID AdmissionRenderCorrelationValidatedWindows(")
        ]
        self.assertNotIn("BrokerBase", begin)
        entry = source.index("AdmissionRenderCorrelationBeginWindows(")
        command_copy = source.index("RtlCopyMemory(&command")
        self.assertLess(entry, command_copy)
        self.assertIn("AdmissionRenderCorrelationQueueExport(Context)", begin)
        self.assertIn("AdmissionRecordUmdRenderGuard", source)
        self.assertIn('L"Wom1UmdRenderGuard"', (
            RENDER / "src" / "receipts.c"
        ).read_text())
        self.assertIn("AdmissionUmdRenderGuardContext", source)
        self.assertIn("AdmissionUmdRenderGuardDevice", source)
        self.assertIn("AdmissionUmdRenderTraceArm(adapter)", (
            RENDER / "src" / "allocation_windows.c"
        ).read_text())
        self.assertIn("UMD_RENDER_RETURN", source)
        self.assertIn("AdmissionUmdRenderGuardUserCopy", source)
        self.assertIn("AdmissionUmdRenderGuardAccepted", source)
        self.assertNotIn(
            "adapter != AdmissionUmdRenderTraceAdapterGet())\n"
            "    UMD_RENDER_RETURN",
            source,
        )
        self.assertIn("J313_AGX_G2_POWER_CMD_QUERY", source)


if __name__ == "__main__":
    unittest.main()
