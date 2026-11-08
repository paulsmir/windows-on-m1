"""IRQ-completion step 1 (investigation/analysis/KMD-IRQ-COMPLETION.md).

Real bugs these invariants catch:
- two consumers of the ASC outbox FIFO (ISR at DIRQL, worker at PASSIVE)
  interleaving the two 64-bit reads of one message: the worker must read the
  hardware FIFO only inside DxgkCbSynchronizeExecution while the line is armed;
- a level line that the ISR returns without quiescing (interrupt storm): the
  mailbox path drains the FIFO before returning TRUE;
- the mailbox resource leaking into the default firmware: the ASL lists the
  level interrupt only inside the AGX_MAILBOX_IRQ preprocessor block and the Mu
  flag defaults to FALSE.
"""
import json
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from generate_j313_agx_abi_admission import (  # noqa: E402
    MAILBOX_BUILD_FLAG,
    load_admission_contract,
    render_asl_include,
    render_m1n1_header,
    render_windows_header,
)

RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"
BACKEND = (RENDER / "src" / "backend_platform_windows.c").read_text()
INTERRUPT = (RENDER / "src" / "interrupt.c").read_text()
LIFECYCLE = (RENDER / "src" / "lifecycle.c").read_text()
HEADER = (RENDER / "include" / "render_admission.h").read_text()
MU_DSC = ROOT / "mu/Platform/MacBookAirMid2020Pkg/MacBookAirMid2020.dsc"


def body(source, name):
    match = re.search(rf"\b{name}\s*\([^;{{]*?\)\s*\{{", source, re.S)
    assert match, name
    depth, index = 1, match.end()
    while depth:
        depth += {"{": 1, "}": -1}.get(source[index], 0)
        index += 1
    return source[match.end():index]


class MailboxInterruptContractTests(unittest.TestCase):
    def test_contract_maps_the_dt_recv_not_empty_line_through_a_g2_route(self):
        contract = load_admission_contract()
        # t8103.dtsi agx_mbox: 575 send-empty, 576 send-not-empty,
        # 577 recv-empty, 578 recv-not-empty; config/j313-agx-g2.json 578 -> 887.
        self.assertEqual(contract.mailbox_recv_physical_interrupt, 578)
        self.assertEqual(contract.mailbox_recv_guest_interrupt, 887)
        self.assertIn(contract.mailbox_recv_guest_interrupt,
                      contract.physical_guest_interrupts)
        self.assertNotEqual(contract.mailbox_recv_guest_interrupt,
                            contract.synthetic_scanout_guest_interrupt)

    def test_asl_lists_the_level_line_only_inside_the_build_flag(self):
        rendered = render_asl_include(load_admission_contract())
        blocks = re.findall(
            rf"#ifdef {MAILBOX_BUILD_FLAG}\n(.*?)#endif", rendered, re.S)
        self.assertEqual(len(blocks), 2)  # BAS0 and LOCR
        for block in blocks:
            self.assertIn(
                "Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)", block)
            self.assertIn("{ 887 }", block)
        outside = re.sub(rf"#ifdef {MAILBOX_BUILD_FLAG}\n.*?#endif", "", rendered, flags=re.S)
        self.assertNotIn("Level", outside)
        for guest in range(880, 889):
            self.assertNotIn(f"{{ {guest} }}", outside)

    def test_windows_header_names_the_line_and_m1n1_header_is_unchanged(self):
        contract = load_admission_contract()
        windows = render_windows_header(contract)
        self.assertIn("J313_AGX_ABI_ADMISSION_MAILBOX_RECV_GUEST_INTID 887u", windows)
        self.assertIn("J313_AGX_ABI_ADMISSION_MAILBOX_RECV_PHYSICAL_INTID 578u", windows)
        self.assertNotIn("MAILBOX", render_m1n1_header(contract))

    def test_mu_flag_defaults_off_and_only_adds_the_aslpp_define(self):
        dsc = MU_DSC.read_text()
        self.assertIn("J313_AGX_MAILBOX_IRQ                       = FALSE", dsc)
        self.assertEqual(dsc.count("-DAGX_MAILBOX_IRQ"), 2)
        for line in dsc.splitlines():
            if "-DAGX_MAILBOX_IRQ" in line:
                self.assertIn("ASLPP_FLAGS", line)


class KmdInterruptStepOneTests(unittest.TestCase):
    def test_resource_validators_accept_one_edge_and_at_most_one_level(self):
        for source, name in ((INTERRUPT, "AdmissionFindBrokerResource"),
                             (LIFECYCLE, "AdmissionG3FirmwareResourcesPresent"),
                             (BACKEND, "AdmissionPlatformValidateResources")):
            validate = body(source, name)
            self.assertIn("CM_RESOURCE_INTERRUPT_LATCHED", validate)
            self.assertIn("CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE", validate)
            self.assertRegex(validate, r"edge_(count|interrupts) == 1u")
            self.assertRegex(validate, r"level_(count|interrupts) <= 1u")

    def test_isr_checks_the_mailbox_before_scanout_and_records_the_census(self):
        isr = body(INTERRUPT, "AdmissionDdiInterruptRoutine")
        self.assertIn("MessageNumber != 0 && MessageNumber != 1", isr)
        self.assertLess(isr.index("AdmissionPlatformMailboxInterrupt("),
                        isr.index("AdmissionScanoutInterrupt("))
        self.assertIn("IrqIsrByMessage[MessageNumber & 3u]", isr)
        self.assertIn("IrqMailboxMessageMask", isr)
        self.assertNotIn("MailboxIrqArmed", isr)  # the platform owns arming

    def test_isr_drains_the_fifo_before_claiming_a_level_interrupt(self):
        handler = body(BACKEND, "AdmissionPlatformMailboxInterrupt")
        drain = body(BACKEND, "AdmissionMailboxDrainFifoAtDirql")
        self.assertIn("AdmissionMailboxDrainFifoAtDirql(runtime)", handler)
        self.assertIn("return FALSE", handler)
        self.assertIn("J313_AGX_G2_ASC_OUTBOX_CTRL_OFFSET", drain)
        self.assertIn("J313_AGX_G2_ASC_OUTBOX0_OFFSET", drain)
        self.assertIn("J313_AGX_G2_ASC_OUTBOX1_OFFSET", drain)
        self.assertIn("APPLE_AGX_ASC_MAILBOX_EMPTY", drain)
        self.assertIn("RingDrops", drain)
        for forbidden in ("KeWait", "KeDelay", "Zw", "ExAllocate", "DbgPrint"):
            self.assertNotIn(forbidden, handler + drain)

    def test_worker_reads_the_hardware_fifo_only_under_the_interrupt_lock(self):
        read32 = body(BACKEND, "AdmissionAscRead32")
        read64 = body(BACKEND, "AdmissionAscRead64")
        self.assertIn("DxgkCbSynchronizeExecution", read32)
        self.assertIn("AdmissionMailboxSyncPop", read32)
        self.assertIn("APPLE_AGX_ASC_MAILBOX_EMPTY : 0u", read32)
        # OUTBOX0/1 served from the ring while armed and non-empty; the raw
        # register read stays only on the else branch.
        self.assertIn("J313_AGX_G2_ASC_OUTBOX0_OFFSET", read64)
        self.assertIn("IrqRing[tail % ADMISSION_IRQ_RING_SLOTS]", read64)
        self.assertIn("RingConsumed++", read64)

    def test_arm_follows_the_acpi_resource_and_disarm_runs_under_both_locks(self):
        self.assertIn("Context->MailboxInterruptVector != 0u ? 1 : 0", BACKEND)
        destroy = body(BACKEND, "AdmissionPlatformDestroy")
        self.assertIn("AdmissionMailboxDisarmed", destroy)
        self.assertIn("message < 2u", destroy)
        self.assertLess(destroy.index("MailboxIrqArmed"), destroy.index("SgxBase"))

    def test_census_receipt_is_exported_only_with_the_resource_present(self):
        self.assertIn("ADMISSION_IRQ_RX_RECEIPT", HEADER)
        export = body(BACKEND, "AdmissionJobTimingExportWindows")
        self.assertIn('L"Wom1IrqRx"', export)
        self.assertIn("MailboxInterruptVector != 0u", export)


if __name__ == "__main__":
    unittest.main()
