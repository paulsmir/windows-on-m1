"""A2: DxgkDdiSystemDisplayEnable/Write source contract (bugcheck display)."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ADMISSION = ROOT / "drivers/apple-agx/render-admission"
SHARED = ROOT / "drivers/apple-agx/shared"


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def function_body(text, name):
    start = text.index(name + "(")
    brace = text.index("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace:index + 1]
    raise AssertionError(name)


class BugcheckDisplayContractTests(unittest.TestCase):
    def setUp(self):
        self.source = (ADMISSION / "src/bugcheck_display_windows.c").read_text()
        self.code = strip_comments(self.source)

    def test_bugcheck_file_is_nonpaged_lockless_and_poolless(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        A pageable, locking, allocating or dxgkrnl-calling path at bugcheck
        IRQL, which hangs or re-crashes the bugcheck and loses the dump.
        """
        for forbidden in (r"#\s*pragma\s+alloc_text", r"#\s*pragma\s+code_seg",
                          r"\bPAGED_CODE\b", r"\bKe\w*Lock\w*\s*\(",
                          r"\bExAllocate\w*", r"\bEx\w*Pool\w*", r"\bDxgkCb\w*",
                          r"\bKeWait\w*", r"\bKeDelay\w*", r"\bZw\w*",
                          r"\bAdmissionRecord\w*", r"\bExAcquire\w*",
                          r"\bKeQueryInterruptTime\b"):
            self.assertNotRegex(self.code, forbidden)
        for name in ("AdmissionDdiSystemDisplayEnable", "AdmissionDdiSystemDisplayWrite"):
            body = function_body(self.code, "VOID " + name if "Write" in name else name)
            for forbidden in (r"\bMm\w+\s*\(", r"\bAdmissionPhysicalRange\w*"):
                self.assertNotRegex(body, forbidden, name)

    def test_enable_poll_is_bounded_and_gated_on_the_capability(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        An unbounded wait at bugcheck (interrupt time does not advance), or a
        REG_EMERGENCY access on a broker without CAP_EMERGENCY_SURFACE (an
        unknown-register abort: a nested crash).
        """
        enable = function_body(self.code, "AdmissionDdiSystemDisplayEnable")
        header = (ADMISSION / "include/render_admission.h").read_text()
        self.assertIn("#define ADMISSION_BUGCHECK_MAX_POLLS 5000u", header)
        self.assertIn("#define ADMISSION_BUGCHECK_POLL_US 100u", header)
        self.assertIn("polls >= ADMISSION_BUGCHECK_MAX_POLLS", enable)
        self.assertIn("KeStallExecutionProcessor(ADMISSION_BUGCHECK_POLL_US)", enable)
        gate = enable.index("display->EmergencyCap && display->BrokerRegs != NULL")
        self.assertLess(gate, enable.index("APPLE_AGX_SCANOUT_REG_EMERGENCY"))
        self.assertLess(gate, enable.index("WRITE_REGISTER_ULONG"))
        self.assertNotIn("STATUS_NOT_SUPPORTED", enable)
        # Black clear, barrier, then the doorbell.
        self.assertLess(enable.index("AdmissionBugcheckClear("), enable.index("__dsb("))
        self.assertLess(enable.index("__dsb("), enable.index("WRITE_REGISTER_ULONG"))
        self.assertIn("BugcheckActive, 1", enable)
        write = function_body(self.code, "VOID AdmissionDdiSystemDisplayWrite")
        self.assertIn("AppleAgxBugcheckBlitRect(", write)
        self.assertIn("__dsb(", write)
        prepare = function_body(self.code, "AdmissionBugcheckDisplayPrepare")
        self.assertIn("APPLE_AGX_SCANOUT_CAP_EMERGENCY_SURFACE", prepare)
        self.assertIn("AppleAgxPostDisplaySurfaceValid(", prepare)
        self.assertIn("AdmissionPhysicalRangeOutsideRam(", prepare)
        self.assertIn("PAGE_WRITECOMBINE", prepare)
        self.assertIn("PAGE_NOCACHE", prepare)
        self.assertTrue(prepare.rstrip("} \n").endswith(
            "InterlockedExchange(&display->Valid, 1);"), "Valid must be published last")

    def test_registered_unwrapped_and_retired_before_post_info_is_cleared(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        The DDIs missing from DRIVER_INITIALIZATION_DATA (a black-screen
        bugcheck), wrapped by the status receipts (locks/registry at bugcheck),
        or a published surface that outlives its mapping or the context.
        """
        driver = (ADMISSION / "src/driver.c").read_text()
        reset = driver.index("initialization.DxgkDdiResetDevice =")
        for name in ("SystemDisplayEnable", "SystemDisplayWrite"):
            line = f"initialization.DxgkDdi{name} = AdmissionDdi{name};"
            self.assertIn(line, driver)
            self.assertLess(reset, driver.index(line))
        wrappers = (ADMISSION / "include/render_ddi_status_wrappers.h").read_text()
        self.assertNotIn("SystemDisplay", wrappers)
        project = (ADMISSION / "AppleAgxRenderAdmission.vcxproj").read_text()
        self.assertIn('<ClCompile Include="src\\bugcheck_display_windows.c" />', project)

        lifecycle = strip_comments((ADMISSION / "src/lifecycle.c").read_text())
        stop = function_body(lifecycle, "static NTSTATUS AdmissionStopDeviceBody")
        self.assertLess(stop.index("AdmissionBugcheckDisplayRetire(context);"),
                        stop.index("RtlZeroMemory(&context->PostDisplayInformation"))
        internal = lifecycle.index("NTSTATUS AdmissionStopDeviceInternal(")
        self.assertIn("AdmissionStopDeviceBody(Context)",
                      function_body(lifecycle[internal:], "AdmissionStopDeviceInternal"))
        remove = function_body(lifecycle, "NTSTATUS AdmissionDdiRemoveDevice")
        self.assertLess(remove.index("AdmissionBugcheckDisplayRetire(context);"),
                        remove.index("ExFreePoolWithTag(context"))
        start = lifecycle.index("DxgkCbAcquirePostDisplayOwnership(\n")
        prepare = lifecycle.index("AdmissionBugcheckDisplayPrepare(context);")
        self.assertLess(start, prepare)
        self.assertLess(lifecycle.index("status = AdmissionScanoutStart(context);"), prepare)
        self.assertLess(prepare, lifecycle.index("APPLE_AGX_WDDM_REQUIRED_READY_MASK);"))

    def test_one_windows_ram_check_and_abi_values(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        A second, diverging Windows-RAM check (a write-combined map over RAM
        the memory manager owns), or a doorbell/status value that differs from
        the m1n1 A1 ABI (bit 10, 0x1c, "BCHK").
        """
        post = strip_comments((ADMISSION / "src/post_display_windows.c").read_text())
        self.assertEqual(post.count("MmGetPhysicalMemoryRangesEx2("), 1)
        self.assertIn("AdmissionPhysicalRangeOutsideRam(Pa, Bytes)",
                      function_body(post, "static ULONG AdmissionStopFillBlack"))
        self.assertNotIn("MmGetPhysicalMemoryRangesEx2", self.code)
        abi = (SHARED / "include/apple_agx_scanout.h").read_text()
        for line in (
            "#define APPLE_AGX_SCANOUT_CAP_EMERGENCY_SURFACE (1u << 10u)",
            "#define APPLE_AGX_SCANOUT_EMERGENCY_MAGIC 0x4B484342u",
            "#define APPLE_AGX_SCANOUT_EMERGENCY_REQUESTED 1u",
            "#define APPLE_AGX_SCANOUT_EMERGENCY_LATCHED_GUEST 2u",
            "#define APPLE_AGX_SCANOUT_EMERGENCY_LATCHED_OWN 3u",
            "#define APPLE_AGX_SCANOUT_EMERGENCY_FAILED 0x80u",
            "APPLE_AGX_SCANOUT_REG_EMERGENCY = 0x1cu,",
        ):
            self.assertIn(line, abi)
        self.assertEqual(bytes.fromhex("4B484342")[::-1], b"BCHK")


if __name__ == "__main__":
    unittest.main()
