import re
import unittest
from pathlib import Path

from tools.platform_stability import (CpuSnapshot, ProbeSample, classify_run,
                                      parse_log_events, parse_watchdog_snapshots)


ROOT = Path(__file__).resolve().parents[1]


def cpu(cpu, progress, *, timer_enabled=False, timer_expired=False,
        timer_queued=False, timer_lr=False, sgi_rate=0, can_stall=True):
    return CpuSnapshot(cpu, progress, timer_enabled, timer_expired,
                       timer_queued, timer_lr, sgi_rate, can_stall)


class PlatformStabilityClassifierTests(unittest.TestCase):
    def test_parser_groups_complete_watchdog_snapshots(self):
        text = """
HV WATCHDOG CPU: cpu=0 pc=0x10 cntpct=0x100 vctl=0x5 vcval=0x80 tq=0 q=4 iar=4 eoi=4 marker=0x11 lr0=0x5020020000000012 lr1=0x0
HV WATCHDOG CPU: cpu=1 pc=0x20 cntpct=0x110 vctl=0x1 vcval=0x120 tq=1 q=7 iar=7 eoi=7 marker=0x22 lr0=0x0
HOST CONTROL: diagnostic snapshot captured; continuing guest
HV WATCHDOG CPU: cpu=0 pc=0x30 cntpct=0x200 vctl=0x1 vcval=0x280 tq=0 q=5 iar=5 eoi=5 marker=0x33 lr0=0x0
HV WATCHDOG CPU: cpu=1 pc=0x20 cntpct=0x210 vctl=0x5 vcval=0x100 tq=0 q=7 iar=7 eoi=7 marker=0x22 lr0=0x0
HOST CONTROL: diagnostic snapshot captured; continuing guest
"""
        snapshots = parse_watchdog_snapshots(text, expected_cpus=2)
        self.assertEqual(len(snapshots), 2)
        self.assertNotEqual(snapshots[0][0].progress, snapshots[1][0].progress)
        self.assertEqual(snapshots[0][1].progress, snapshots[1][1].progress)
        self.assertTrue(snapshots[0][0].timer_enabled)
        self.assertTrue(snapshots[0][0].timer_expired)
        self.assertTrue(snapshots[0][0].timer_lr)
        self.assertTrue(snapshots[0][1].timer_queued)

    def test_parser_discards_incomplete_snapshot(self):
        text = """HV WATCHDOG CPU: cpu=0 pc=0 cntpct=1 vctl=0 tq=0 q=0 iar=0 eoi=0 marker=0 lr0=0
HOST CONTROL: diagnostic snapshot captured; continuing guest
"""
        self.assertEqual(parse_watchdog_snapshots(text, expected_cpus=2), [])

    def test_parser_handles_cpu_records_interleaved_on_one_uart_line(self):
        text = """HV WATCHDOG CPU: cpu=0 pc=0x10 vctl=0x5 tq=0 q=4 iar=4 eoi=4 marker=0x11 lr0=0x12 HV WATCHDOG CPU: cpu=1 pc=0x20 vctl=0x1 tq=0 q=7 iar=7 eoi=7 marker=0x22 lr0=0 [cpu0] User interrupt
HOST CONTROL: diagnostic snapshot captured; continuing guest
HV WATCHDOG CPU: cpu=0 pc=0x30 vctl=0x5 tq=0 q=5 iar=5 eoi=5 marker=0x33 lr0=0x12 HV WATCHDOG CPU: cpu=1 pc=0x40 vctl=0x1 tq=0 q=8 iar=8 eoi=8 marker=0x44 lr0=0
HOST CONTROL: diagnostic snapshot captured; continuing guest
"""
        snapshots = parse_watchdog_snapshots(text, expected_cpus=2)
        self.assertEqual(len(snapshots), 2)
        self.assertEqual([item.cpu for item in snapshots[0]], [0, 1])
        self.assertTrue(snapshots[0][0].timer_lr)

    def test_raw_watchdog_idle_pc_is_not_sufficient_stall_evidence(self):
        text = """
HV WATCHDOG CPU: cpu=0 pc=0x10 cntpct=0x100 vctl=0x1 tq=0 q=4 iar=4 eoi=4 marker=0x11 lr0=0
HV WATCHDOG CPU: cpu=1 pc=0x20 cntpct=0x110 vctl=0x1 tq=0 q=7 iar=7 eoi=7 marker=0x22 lr0=0
HOST CONTROL: diagnostic snapshot captured; continuing guest
HV WATCHDOG CPU: cpu=0 pc=0x30 cntpct=0x200 vctl=0x1 tq=0 q=5 iar=5 eoi=5 marker=0x33 lr0=0
HV WATCHDOG CPU: cpu=1 pc=0x20 cntpct=0x210 vctl=0x1 tq=0 q=7 iar=7 eoi=7 marker=0x22 lr0=0
HOST CONTROL: diagnostic snapshot captured; continuing guest
"""
        snapshots = parse_watchdog_snapshots(text, expected_cpus=2)
        self.assertEqual(classify_run([], snapshots, [], []).kind, "incomplete")

    def test_bugcheck_has_highest_priority(self):
        result = classify_run([], [], [], [{"kind": "bugcheck", "code": 0x101}])
        self.assertEqual(result.kind, "bugcheck")
        self.assertEqual(result.details["code"], 0x101)

    def test_log_parser_preserves_bugcheck_and_parameters(self):
        events = parse_log_events(
            "TTY> HV BUGCHECK: seen_by_cpu=0 code=0x133 "
            "P1=0x1 P2=0x1e00 P3=0xfffff801740083b0 P4=0x0\n"
        )
        self.assertEqual(events, [{
            "kind": "bugcheck",
            "seen_by_cpu": 0,
            "code": 0x133,
            "p1": 1,
            "p2": 0x1e00,
            "p3": 0xfffff801740083b0,
            "p4": 0,
        }])

    def test_stale_framebuffer_with_live_ssh_is_ui_pause(self):
        probes = [ProbeSample(0, True, 40), ProbeSample(3, True, 43)]
        self.assertEqual(classify_run([], [], probes, []).kind, "ui_pause")

    def test_early_disconnect_is_transport_loss(self):
        links = [{"kind": "disconnect", "complete_snapshot": False}]
        self.assertEqual(classify_run([], [], [], links).kind, "transport_loss")

    def test_expired_timer_without_queue_or_lr_is_timer_loss(self):
        snapshots = [
            [cpu(0, 10), cpu(1, 10, timer_enabled=True, timer_expired=True)],
            [cpu(0, 20), cpu(1, 10, timer_enabled=True, timer_expired=True)],
        ]
        result = classify_run([], snapshots, [], [])
        self.assertEqual(result.kind, "timer_loss")
        self.assertEqual(result.cpu, 1)

    def test_sgi_storm_requires_two_seconds(self):
        snapshots = [[cpu(0, 10, sgi_rate=10_001)], [cpu(0, 20, sgi_rate=10_001)]]
        self.assertEqual(classify_run([], snapshots, [], []).kind, "sgi_storm")

    def test_one_stalled_cpu_with_advancing_peer_is_cpu_stall(self):
        snapshots = [[cpu(0, 10), cpu(1, 10)], [cpu(0, 20), cpu(1, 10)]]
        result = classify_run([], snapshots, [], [])
        self.assertEqual(result.kind, "cpu_stall")
        self.assertEqual(result.cpu, 1)

    def test_all_cpus_stalled_with_live_link_is_guest_freeze(self):
        snapshots = [[cpu(0, 10), cpu(1, 10)], [cpu(0, 10), cpu(1, 10)]]
        links = [{"kind": "sample", "complete_snapshot": True}]
        self.assertEqual(classify_run([], snapshots, [], links).kind, "guest_freeze")

    def test_fresh_boot_banner_is_host_reset(self):
        self.assertEqual(classify_run([], [], [], [{"kind": "boot"}]).kind,
                         "host_reset")

    def test_advancing_cpus_and_ssh_are_healthy(self):
        snapshots = [[cpu(0, 10), cpu(1, 10)], [cpu(0, 20), cpu(1, 20)]]
        probes = [ProbeSample(0, True, 3), ProbeSample(3, True, 0)]
        self.assertEqual(classify_run([], snapshots, probes, []).kind, "healthy")


class HcrMutationPolicyTests(unittest.TestCase):
    def test_runtime_whole_hcr_writes_are_policy_guarded(self):
        source_root = ROOT / "m1n1_windows" / "src"
        direct_writes = []
        for path in source_root.glob("*.c"):
            text = path.read_text(encoding="utf-8")
            for match in re.finditer(r"msr\(HCR_EL2,", text):
                direct_writes.append((path.name, text.count("\n", 0, match.start()) + 1))

        # exception.c establishes the pre-hypervisor baseline.  hv.c owns the
        # only policy-aware runtime helper and secondary-entry restore.
        self.assertEqual(
            sorted(name for name, _line in direct_writes),
            ["exception.c", "hv.c", "hv.c", "hv.c"],
            direct_writes,
        )
        hv = (source_root / "hv.c").read_text(encoding="utf-8")
        self.assertIn("void hv_write_hcr(u64 val)", hv)
        self.assertIn("val = hv_wfx_apply_hcr(val);", hv)
        self.assertIn("msr(HCR_EL2, hv_wfx_apply_hcr(info->hcr));", hv)


class AssistedSupervisorPolicyTests(unittest.TestCase):
    def test_each_invocation_uses_a_unique_capture_directory(self):
        source = (ROOT / "scripts" / "supervise-assisted.sh").read_text(encoding="utf-8")

        self.assertIn('RUN_ID=$(date -u +%Y%m%dT%H%M%SZ)-$$', source)
        self.assertIn('CAPTURE_ROOT="$CAPTURE_BASE/$RUN_ID"', source)

    def test_ssh_failure_alone_cannot_reset_the_guest(self):
        source = (ROOT / "scripts" / "supervise-assisted.sh").read_text(encoding="utf-8")

        self.assertNotIn("sustained health failure; requesting snapshot+reboot", source)
        self.assertNotIn('kill -TERM "$runner"', source)
        self.assertIn("ssh unavailable; guest reset suppressed", source)

    def test_ssh_failure_captures_two_non_destructive_snapshots_and_classifies(self):
        source = (ROOT / "scripts" / "supervise-assisted.sh").read_text(encoding="utf-8")

        self.assertGreaterEqual(source.count('kill -INT "$runner"'), 2)
        self.assertIn("tools/platform_stability.py", source)
        self.assertIn("stability-classification.json", source)


if __name__ == "__main__":
    unittest.main()
