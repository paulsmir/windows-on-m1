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
    def test_virtual_irq_recompute_is_not_on_every_serialized_exception(self):
        exc = HV_EXC.read_text()
        exit_body = function_body(exc, "static void hv_exc_exit(struct exc_info *ctx)")

        self.assertIn("hv_update_fiq();", exit_body)
        self.assertNotIn("hv_vgic3_update_vi();", exit_body)

    def test_maintenance_lr_clear_recomputes_virtual_irq_line(self):
        exc = HV_EXC.read_text()
        irq = function_body(exc, "void hv_exc_irq(struct exc_info *ctx)")
        maintenance = irq.split("if(irq == 0 || type == 0)", 1)[1].split(
            "return;", 1)[0]
        self.assertIn("hv_vgic3_write_lr(lr, 0);", maintenance)
        self.assertIn("hv_vgic3_update_vi();", maintenance)
        self.assertLess(maintenance.index("hv_vgic3_write_lr(lr, 0);"),
                        maintenance.index("hv_vgic3_update_vi();"))

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
