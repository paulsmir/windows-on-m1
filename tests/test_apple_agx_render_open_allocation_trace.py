from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"
DECODER = RENDER / "scripts" / "decode-open-allocation-trace.py"


def load_decoder():
    spec = spec_from_file_location("decode_open_allocation_trace", DECODER)
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class OpenAllocationTraceTests(unittest.TestCase):
    def test_decoder_requires_complete_monotonic_receipt(self):
        decoder = load_decoder()
        lines = []
        for field in sorted(decoder.NAMES):
            value = 0 if field in (10, 11) else field + 20
            word = (0x5130 << 48) | (field << 32) | value
            lines.append(
                f"TTY> HV: AGX power receipt seq={word} cmd=0 state=3 result=0"
            )
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "host.log"
            path.write_text("\n".join(lines))
            decoded = decoder.decode(path)
        self.assertEqual(decoded["version"], 21)
        self.assertEqual(decoded["allocation_handle"], 27)
        self.assertEqual(decoded["guard"], 0)
        self.assertEqual(decoded["status"], 0)

    def test_open_callback_records_acquire_and_description_guards(self):
        header = (RENDER / "include" / "render_submit_trace.h").read_text()
        context = (RENDER / "include" / "render_admission.h").read_text()
        source = (RENDER / "src" / "allocation_windows.c").read_text()
        self.assertIn("ADMISSION_OPEN_ALLOCATION_TRACE_TAG", header)
        self.assertIn("AdmissionOpenAllocationTraceWord", header)
        self.assertIn("OpenAllocationTraceClaimed", context)
        self.assertIn("OPEN_ALLOCATION_RETURN", source)
        self.assertIn("AdmissionOpenAllocationGuardAcquire", source)
        self.assertIn("AdmissionOpenAllocationGuardDescription", source)
        self.assertIn("AdmissionOpenAllocationGuardAccepted", source)
        self.assertIn("#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)", source)
        self.assertIn("J313_AGX_G2_POWER_CMD_QUERY", source)


if __name__ == "__main__":
    unittest.main()
