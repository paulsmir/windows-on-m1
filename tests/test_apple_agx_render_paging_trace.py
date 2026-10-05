from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"
DECODER = RENDER / "scripts" / "decode-paging-build-trace.py"

def load_decoder():
    spec = spec_from_file_location("decode_paging_build_trace", DECODER)
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

class PagingBuildTraceTests(unittest.TestCase):
    def test_decoder_requires_exact_first_call(self):
        decoder = load_decoder()
        lines = []
        for field in sorted(decoder.NAMES):
            value = 0 if field == 6 else field + 40
            word = (0x5140 << 48) | (field << 32) | value
            lines.append(f"TTY> HV: AGX power receipt seq={word} cmd=0 state=3 result=0")
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "host.log"
            path.write_text("\n".join(lines))
            result = decoder.decode(path)
        self.assertEqual(result["operation"], 43)
        self.assertEqual(result["status"], 0)

    def test_wrapper_observes_unchanged_inner_implementation(self):
        header = (RENDER / "include/render_submit_trace.h").read_text()
        context = (RENDER / "include/render_admission.h").read_text()
        source = (RENDER / "src/paging_windows.c").read_text()
        self.assertIn("ADMISSION_PAGING_BUILD_TRACE_TAG", header)
        self.assertIn("PagingBuildTraceClaimed", context)
        self.assertIn("static NTSTATUS AdmissionBuildPagingBuffer", source)
        self.assertIn("NTSTATUS AdmissionDdiBuildPagingBuffer", source)
        self.assertIn("status = AdmissionBuildPagingBuffer", source)
        self.assertIn("AdmissionPagingBuildTraceWord", source)
        self.assertIn("#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)", source)

    def test_trace_is_armed_only_by_exact_producer_allocation(self):
        abi = (RENDER / "include/render_umd_command.h").read_text()
        context = (RENDER / "include/render_admission.h").read_text()
        allocation = (RENDER / "src/allocation_windows.c").read_text()
        paging = (RENDER / "src/paging_windows.c").read_text()
        producer = (ROOT / "drivers/apple-agx/windows/one-shot/apple_agx_d3dkmt_render.c").read_text()
        self.assertIn("ADMISSION_UMD_CORRELATION_COOKIE", abi)
        self.assertIn("PagingCorrelationArmed", context)
        self.assertIn("QualificationCookie", context)
        self.assertIn("normalized.Reserved = 0u", allocation)
        self.assertIn("PagingCorrelationArmed", allocation)
        self.assertIn("PagingCorrelationArmed", paging)
        self.assertIn("allocation[0].Reserved = ADMISSION_UMD_CORRELATION_COOKIE", producer)

if __name__ == "__main__":
    unittest.main()
