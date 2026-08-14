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

    def test_secondary_fiq_uses_accepted_virtual_irq_exit_contract(self):
        exc = HV_EXC.read_text()
        fiq = function_body(exc, "void hv_exc_fiq(struct exc_info *ctx)")
        fast = fiq.split("if (secondary_fast) {", 1)[1].split(
            "// Slow (single threaded) path", 1
        )[0]

        self.assertEqual(fast.count("hv_vgic3_update_vi();"), 1)
        self.assertLess(fast.index("hv_update_fiq();"),
                        fast.index("hv_vgic3_update_vi();"))
        self.assertLess(fast.index("hv_handle_local_ipi();"),
                        fast.index("hv_vgic3_update_vi();"))
        self.assertLess(fast.index("hv_vgic3_update_vi();"),
                        fast.index("hv_fiq_secondary_fast_complete("))
        self.assertIn(
            "hv_fiq_secondary_fast_complete(true, !!(mrs(ISR_EL1) & 0x40),",
            fast,
        )
        self.assertIn("!!(mrs(HCR_EL2) & HCR_VI)", fast)

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

    def test_sgi_queue_console_trace_requires_explicit_hot_path_tracing(self):
        exc = HV_EXC.read_text()
        queue = function_body(exc, "static void hv_vgic3_queue_sgi(int cpu, u32 intid)")

        self.assertIn("hv_runtime_trace_enabled()", queue)
        self.assertNotIn("hv_runtime_diag_enabled()", queue)

    def test_timer_rate_console_trace_requires_explicit_hot_path_tracing(self):
        exc = HV_EXC.read_text()
        update = function_body(exc, "static void hv_update_fiq(void)")

        self.assertIn("hv_runtime_trace_enabled()", update)
        self.assertNotIn("hv_runtime_diag_enabled() && (++dbg_inj_count", update)

    def test_timer_delivery_uses_the_accepted_latch_and_repend_path(self):
        exc = HV_EXC.read_text()
        update = function_body(exc, "static void hv_update_fiq(void)")

        self.assertIn("timer_irq_outstanding", update)
        self.assertIn("timer_repend_live_irq", update)

    def test_live_virtual_timer_lr_tracks_asserted_and_deasserted_level(self):
        exc = HV_EXC.read_text()
        sync = function_body(exc, "static bool timer_sync_live_irq")
        update = function_body(exc, "static void hv_update_fiq(void)")
        virtual = update.split("if (mrs(CNTV_CTL_EL02)", 1)[1]

        self.assertIn("hv_vgic_diag_sync_level_lr", sync)
        self.assertIn("if (next.changed)", sync)
        self.assertIn("hv_vgic3_write_lr", sync)
        self.assertIn("hv_vgic3_update_vi();", sync)
        self.assertIn("timer_sync_live_irq(18, true)", virtual)
        self.assertIn("timer_sync_live_irq(18, false);", virtual)
        self.assertNotIn("timer_sync_live_irq(17", update)
        self.assertNotIn("hv_sync_timer_level", update)

    def test_deliverable_timer_vi_edge_defers_wake_until_final_fiq_return(self):
        exc = HV_EXC.read_text()
        vgic = HV_VGIC.read_text()
        update = function_body(vgic, "void hv_vgic3_update_vi(void)")
        flush = function_body(vgic, "void hv_vgic3_flush_timer_wake(void)")
        fiq = function_body(exc, "void hv_exc_fiq(struct exc_info *ctx)")

        self.assertIn("bool timer_signal = false;", update)
        self.assertIn("intid == 17 || intid == 18", update)
        self.assertIn("hv_vgic_diag_needs_timer_edge_wake", update)
        self.assertIn("hv_vgic3_defer_timer_wake();", update)
        self.assertNotIn("smp_send_ipi", update)
        self.assertIn('sysop("isb");', flush)
        self.assertIn("smp_send_ipi(cpu);", flush)
        self.assertLess(fiq.rindex("hv_handle_local_ipi();"),
                        fiq.rindex("hv_exc_exit(ctx);"))
        self.assertLess(fiq.rindex("hv_exc_exit(ctx);"),
                        fiq.rindex("hv_vgic3_flush_timer_wake();"))

    def test_timer_deassertion_clears_the_accepted_delivery_latch(self):
        exc = HV_EXC.read_text()
        update = function_body(exc, "static void hv_update_fiq(void)")

        self.assertIn("timer_p_injected[tcpu] = false;", update)
        self.assertIn("timer_v_injected[tcpu] = false;", update)

    def test_timer_eoi_does_not_add_the_rejected_level_state_drain(self):
        vgic = HV_VGIC.read_text()
        eoi = function_body(vgic, "void hv_vgic3_do_eoir1(u64 reg)")

        self.assertNotIn("hv_vgic3_drain_timer_queue();", eoi)

    def test_spurious_iar_console_trace_requires_explicit_hot_path_tracing(self):
        vgic = HV_VGIC.read_text()
        iar = function_body(vgic, "int hv_vgic3_do_iar1(void)")
        guarded = re.search(
            r"if\s*\(hv_runtime_trace_enabled\(\)\s*&&\s*"
            r"__atomic_fetch_add\(&spurious_iar,\s*1,\s*__ATOMIC_RELAXED\)\s*<\s*16\)",
            iar,
        )
        self.assertIsNotNone(guarded)

    def test_xhci_route_marks_the_end_of_boot_tick_cadence(self):
        vgic = HV_VGIC.read_text()
        dist = function_body(vgic, "static bool handle_vgic_dist_access(")

        self.assertIn("hv_mark_guest_runtime_ready();", dist)
        self.assertLess(dist.index("hv_prepare_j313_xhci_handoff();"),
                        dist.index("hv_mark_guest_runtime_ready();"))
        self.assertLess(dist.index("hv_mark_guest_runtime_ready();"),
                        dist.index("aic_set_mask(route->hw_irq, false);"))

    def test_watchdog_snapshot_reports_global_bhl_owner_and_depth(self):
        exc = HV_EXC.read_text()
        dump = function_body(exc, "void hv_watchdog_snapshot_dump(void)")

        self.assertIn("HV WATCHDOG BHL: owner=", dump)
        self.assertIn("__atomic_load_n(&bhl.lock", dump)
        self.assertIn("__atomic_load_n(&bhl.count", dump)

    def test_watchdog_snapshot_reports_host_tick_arm_and_fire_counters(self):
        exc = HV_EXC.read_text()
        sample = function_body(exc, "void hv_watchdog_snapshot_tick(struct exc_info *ctx)")
        dump = function_body(exc, "void hv_watchdog_snapshot_dump(void)")

        self.assertIn("CNTP_CTL_EL0", sample)
        self.assertIn("CNTP_CVAL_EL0", sample)
        self.assertIn("hv_tick_arm_count", sample)
        self.assertIn("hv_recovery_tick_arm_count", sample)
        self.assertIn("host_tick_fires", sample)
        self.assertIn("host_pctl=", dump)
        self.assertIn("tick_arm=", dump)

    def test_watchdog_snapshot_formats_lr_bank_without_overflowing_cpu_record(self):
        exc = HV_EXC.read_text()
        dump = function_body(exc, "void hv_watchdog_snapshot_dump(void)")

        self.assertIn('"marker=0x%lx ",', dump)
        self.assertIn('printf("lrc=%lu lr0=0x%lx lr1=0x%lx lr2=0x%lx "', dump)
        self.assertIn('lr7=0x%lx\\n",', dump)
        self.assertNotIn('"marker=0x%lx lrc=%lu', dump)

    def test_release_build_skips_snapshot_hot_path_before_sampling(self):
        exc = HV_EXC.read_text()
        sample = function_body(exc, "void hv_watchdog_snapshot_tick(struct exc_info *ctx)")

        self.assertIn("if (!hv_runtime_diag_enabled())", sample)
        self.assertLess(sample.index("if (!hv_runtime_diag_enabled())"),
                        sample.index("watchdog_sample_ticks"))

    def test_fiq_does_not_arm_the_rejected_one_millisecond_recovery_tick(self):
        exc = HV_EXC.read_text()
        fiq = function_body(exc, "void hv_exc_fiq(struct exc_info *ctx)")

        self.assertNotIn("hv_arm_guest_irq_recovery_tick();", fiq)


if __name__ == "__main__":
    unittest.main()
