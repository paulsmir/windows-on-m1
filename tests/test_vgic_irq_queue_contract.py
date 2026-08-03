import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1] / "m1n1_windows" / "src"
HV_EXC = ROOT / "hv_exc.c"
HV_VGIC = ROOT / "hv_vgic.c"


def function_body(source, name):
    start = source.index(name)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : pos]
    raise AssertionError(f"unterminated function {name}")


class VgicIrqQueueContractTest(unittest.TestCase):
    def test_software_eoi_drains_pending_hardware_irq_queue(self):
        exc = HV_EXC.read_text()
        vgic = HV_VGIC.read_text()

        drain = function_body(exc, "void hv_vgic3_drain_irq_queue(void)")
        self.assertIn("virq_queue_pop(&PERCPU(irq_queue)", drain)
        self.assertIn("hv_vgic3_inject_irq(", drain)

        eoi = function_body(vgic, "void hv_vgic3_do_eoir1(u64 reg)")
        self.assertIn("hv_vgic3_drain_irq_queue();", eoi)

    def test_failed_queue_insert_cannot_leave_level_irq_masked_forever(self):
        exc = HV_EXC.read_text()
        irq = function_body(exc, "void hv_exc_irq(struct exc_info *ctx)")
        self.assertRegex(
            irq,
            r"if\s*\(!virq_queue_push\(&PERCPU\(irq_queue\),\s*&pending\)"
            r"\s*&&\s*route\s*&&\s*route->level\)"
            r"(?s:.*?)aic_set_mask\(route->hw_irq, false\)",
        )


if __name__ == "__main__":
    unittest.main()
