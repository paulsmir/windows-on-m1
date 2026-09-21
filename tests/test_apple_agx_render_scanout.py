from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", source, re.S)
    if match is None:
        raise AssertionError(f"function {name} was not found")
    start = match.end()
    depth = 1
    index = start
    while index < len(source) and depth:
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
        index += 1
    if depth:
        raise AssertionError(f"function {name} has an unterminated body")
    return source[start:index - 1]


class AppleAgxRenderScanoutTests(unittest.TestCase):
    def test_project_links_the_production_scanout_owner(self):
        project = (RENDER / "AppleAgxRenderAdmission.vcxproj").read_text()

        self.assertIn(r"src\scanout_windows.c", project)
        self.assertIn(r"..\shared\src\apple_agx_scanout.c", project)
        self.assertIn(r"..\shared\src\apple_agx_fixed_panel.c", project)

    def test_scanout_lifetime_is_inside_post_and_memory_ownership(self):
        lifecycle = (RENDER / "src" / "lifecycle.c").read_text()
        start = function_body(lifecycle, "AdmissionDdiStartDevice")
        stop = function_body(lifecycle, "AdmissionDdiStopDevice")
        remove = function_body(lifecycle, "AdmissionDdiRemoveDevice")

        self.assertLess(start.index("DxgkCbAcquirePostDisplayOwnership("),
                        start.index("AdmissionScanoutStart(context)"))
        self.assertLess(start.index("AdmissionScanoutStart(context)"),
                        start.index("AdmissionObjectsStartAdapter("))
        self.assertLess(stop.index("AdmissionScanoutStop(context)"),
                        stop.index("AdmissionMemoryRuntimeStop(context)"))
        self.assertIn("context->ScanoutRuntime != NULL", remove)
        self.assertLess(remove.index("AdmissionScanoutStop(context)"),
                        remove.index("AdmissionMemoryRuntimeStop(context)"))

    def test_vidpn_paths_delegate_to_the_registered_fixed_panel(self):
        display = (RENDER / "src" / "display.c").read_text()
        visibility = function_body(
            display, "AdmissionDdiSetVidPnSourceVisibility")
        commit = function_body(display, "AdmissionDdiCommitVidPn")
        source_address = function_body(
            display, "AdmissionDdiSetVidPnSourceAddress")

        self.assertIn("AdmissionScanoutSetVisible(", visibility)
        self.assertIn("AdmissionScanoutCommit(", commit)
        self.assertIn("AdmissionScanoutSetVisible(context, FALSE)", commit)
        self.assertIn("AdmissionScanoutQueuePresent(", source_address)
        self.assertIn("SetVidPnSourceAddress->hAllocation", source_address)
        self.assertNotIn("STATUS_NOT_SUPPORTED", source_address)

    def test_dirql_enqueue_and_isr_are_bounded(self):
        scanout = (RENDER / "src" / "scanout_windows.c").read_text()
        enqueue = function_body(scanout, "AdmissionScanoutQueuePresent")
        interrupt = function_body(scanout, "AdmissionScanoutInterrupt")
        forbidden = (
            "KeWaitForSingleObject",
            "KeDelayExecutionThread",
            "ExAllocatePool",
            "Zw",
            "AppleAgxScanoutWaitForReceipt",
            "AppleAgxFixedPanelPresent(",
        )

        for symbol in forbidden:
            self.assertNotIn(symbol, enqueue)
            self.assertNotIn(symbol, interrupt)
        self.assertIn("AppleAgxFixedPanelQueuePresent(", enqueue)
        self.assertIn("ADMISSION_ALLOCATION_OBJECT_MAGIC", enqueue)
        self.assertIn("D3DDDIFMT_A8R8G8B8", enqueue)
        self.assertIn("AppleAgxScanoutConsumeInterrupt(", interrupt)
        self.assertIn("DXGK_INTERRUPT_CRTC_VSYNC", interrupt)

    def test_isr_consumes_scanout_before_any_legacy_ack(self):
        interrupt = (RENDER / "src" / "interrupt.c").read_text()
        isr = function_body(interrupt, "AdmissionDdiInterruptRoutine")
        control = function_body(interrupt, "AdmissionDdiControlInterrupt")

        self.assertIn("AdmissionScanoutInterrupt(context)", isr)
        if "AdmissionAcknowledgeInterrupt(context)" in isr:
            self.assertLess(isr.index("AdmissionScanoutInterrupt(context)"),
                            isr.index("AdmissionAcknowledgeInterrupt(context)"))
        self.assertIn("AdmissionScanoutControlInterrupt(", control)

    def test_full_graphics_present_accepts_only_null_dma_exact_primary(self):
        callbacks = (RENDER / "src" / "callbacks.c").read_text()
        present = function_body(callbacks, "AdmissionDdiPresent")

        self.assertIn("pDmaBuffer != NULL", present)
        self.assertIn("D3DDDIFMT_A8R8G8B8", present)
        self.assertIn("ADMISSION_OPEN_ALLOCATION_MAGIC", present)
        self.assertIn("return STATUS_SUCCESS", present)


if __name__ == "__main__":
    unittest.main()
