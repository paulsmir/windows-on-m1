import struct
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import hang_telemetry
from hang_telemetry import (
    TelemetryRecorder,
    TelemetryProtocolError,
    classify_window,
    delta,
    parse_sample,
    parse_status,
)


STATUS = struct.Struct("<IIIIQQ")
SAMPLE = struct.Struct("<18Q4I8H")
U64_FIELDS = (
    "sequence", "host_fiq_count", "host_tick_count", "guest_pc", "guest_spsr",
    "nvme_sq_doorbells", "nvme_cq_doorbells", "nvme_commands", "nvme_completions",
    "nvme_irq_injects", "nvme_irq_iars", "nvme_irq_eois", "xhci_hw_irqs",
    "xhci_irq_injects", "xhci_irq_iars", "xhci_irq_eois", "fb_completed_frames",
    "fb_backpressure_skips",
)
U32_FIELDS = ("vgic_pending_lrs", "vgic_active_lrs", "vgic_occupied_lrs", "flags")


def packed_sample(**changes):
    values = {name: 0 for name in U64_FIELDS + U32_FIELDS}
    values.update(changes)
    queues = changes.get("queues", ((0, 0, 0, 0), (0, 0, 0, 0)))
    return SAMPLE.pack(
        *(values[name] for name in U64_FIELDS),
        *(values[name] for name in U32_FIELDS),
        *queues[0],
        *queues[1],
    )


def sample(**changes):
    return parse_sample(packed_sample(**changes))


class TelemetryParserTests(unittest.TestCase):
    def test_status_validates_version_size_and_length(self):
        status = parse_status(STATUS.pack(1, 176, 256, 7, 12, 19))
        self.assertEqual(
            (status.abi_version, status.sample_size, status.capacity, status.count,
             status.oldest_sequence, status.next_sequence),
            (1, 176, 256, 7, 12, 19),
        )
        for payload in (
            b"\0" * 31,
            STATUS.pack(2, 176, 256, 0, 0, 0),
            STATUS.pack(1, 175, 256, 0, 0, 0),
        ):
            with self.subTest(payload=payload), self.assertRaises(TelemetryProtocolError):
                parse_status(payload)

    def test_sample_decodes_every_literal_field(self):
        payload = SAMPLE.pack(*range(1, 31))
        value = parse_sample(payload)
        self.assertEqual(tuple(getattr(value, name) for name in U64_FIELDS), tuple(range(1, 19)))
        self.assertEqual(tuple(getattr(value, name) for name in U32_FIELDS), tuple(range(19, 23)))
        self.assertEqual(value.queues, ((23, 24, 25, 26), (27, 28, 29, 30)))
        with self.assertRaises(TelemetryProtocolError):
            parse_sample(payload[:-1])

    def test_delta_is_saturating_for_counter_reset(self):
        older = sample(sequence=8, host_tick_count=100, nvme_commands=20)
        newer = sample(sequence=9, host_tick_count=3, nvme_commands=25)
        changes = delta(older, newer)
        self.assertEqual(changes["host_tick_count"], 3)
        self.assertEqual(changes["nvme_commands"], 5)


class TelemetryClassifierTests(unittest.TestCase):
    def test_moving_pc_and_progressing_counters_is_running(self):
        window = [
            sample(sequence=i, guest_pc=0x1000 + 4 * i, host_fiq_count=10 * i,
                   host_tick_count=10 * i, nvme_commands=i, nvme_completions=i,
                   nvme_irq_iars=i, xhci_hw_irqs=i, xhci_irq_iars=i)
            for i in range(4)
        ]
        self.assertEqual(classify_window(window), ("running",))

    def test_static_pc_with_live_timer_is_distinguished_from_dead_el2(self):
        window = [
            sample(sequence=i, guest_pc=0x2000, host_fiq_count=10 * i,
                   host_tick_count=10 * i)
            for i in range(4)
        ]
        self.assertEqual(classify_window(window), ("timer-progress", "guest-pc-static"))

    def test_nvme_command_without_completion_requires_three_intervals(self):
        window = [
            sample(sequence=i, guest_pc=0x3000, host_fiq_count=10 * i,
                   host_tick_count=10 * i, nvme_commands=(1 if i else 0),
                   nvme_completions=0)
            for i in range(4)
        ]
        self.assertIn("nvme-command-without-cqe", classify_window(window))
        self.assertEqual(classify_window(window[:2]), ("insufficient-evidence",))

    def test_completion_without_nvme_iar_is_reported(self):
        window = [
            sample(sequence=i, guest_pc=0x4000, host_fiq_count=10 * i,
                   host_tick_count=10 * i, nvme_completions=i, nvme_irq_iars=0)
            for i in range(4)
        ]
        self.assertIn("nvme-cqe-without-iar", classify_window(window))

    def test_physical_xhci_irq_without_guest_iar_is_reported(self):
        window = [
            sample(sequence=i, guest_pc=0x5000, host_fiq_count=10 * i,
                   host_tick_count=10 * i, xhci_hw_irqs=i, xhci_irq_iars=0)
            for i in range(4)
        ]
        self.assertIn("xhci-hw-without-iar", classify_window(window))

    def test_framebuffer_backpressure_is_a_warning_not_guest_stall(self):
        window = [
            sample(sequence=i, guest_pc=0x6000 + i * 4, host_fiq_count=10 * i,
                   host_tick_count=10 * i, fb_backpressure_skips=i)
            for i in range(4)
        ]
        self.assertEqual(classify_window(window), ("running", "framebuffer-backpressure"))

    def test_one_unchanged_interval_is_insufficient_evidence(self):
        unchanged = sample(sequence=1, guest_pc=0x7000, host_fiq_count=1, host_tick_count=1)
        self.assertEqual(classify_window([unchanged, unchanged]), ("insufficient-evidence",))


class FakeClock:
    def __init__(self, now=0.0):
        self.now = now

    def __call__(self):
        return self.now


class FakeProxy:
    def __init__(self, status, samples):
        self.status = status
        self.samples = samples
        self.status_calls = 0
        self.sample_calls = []

    def hv_diag_status(self):
        self.status_calls += 1
        if isinstance(self.status, Exception):
            raise self.status
        return self.status

    def hv_diag_sample(self, sequence):
        self.sample_calls.append(sequence)
        return self.samples.get(sequence)


class TelemetryRecorderTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        self.root = Path(self.tempdir.name)
        self.log_path = self.root / "hang-telemetry.jsonl"
        self.status_path = self.root / "hang-telemetry-status.json"
        self.clock = FakeClock(100.0)

    def make_recorder(self, proxy, interval=5.0):
        return TelemetryRecorder(
            proxy,
            log_path=self.log_path,
            status_path=self.status_path,
            interval=interval,
            clock=self.clock,
        )

    def test_poll_reads_retained_sequences_in_order_and_publishes_status(self):
        samples = {
            sequence: packed_sample(
                sequence=sequence,
                guest_pc=0x1000 + sequence * 4,
                host_fiq_count=sequence * 10,
                host_tick_count=sequence * 10,
            )
            for sequence in range(10, 14)
        }
        proxy = FakeProxy(STATUS.pack(1, 176, 256, 4, 10, 14), samples)
        recorder = self.make_recorder(proxy)

        self.assertTrue(recorder.maybe_poll())
        self.assertEqual(proxy.sample_calls, [10, 11, 12, 13])
        records = [json.loads(line) for line in self.log_path.read_text().splitlines()]
        self.assertEqual([record["sequence"] for record in records], [10, 11, 12, 13])
        published = json.loads(self.status_path.read_text())
        self.assertEqual(published["last_sequence"], 13)
        self.assertEqual(published["findings"], ["running"])
        self.assertEqual(published["ring"], {"capacity": 256, "count": 4,
                                              "oldest_sequence": 10,
                                              "next_sequence": 14})

    def test_interval_limits_calls_and_next_poll_resumes_after_last_sequence(self):
        proxy = FakeProxy(
            STATUS.pack(1, 176, 256, 1, 5, 6),
            {5: packed_sample(sequence=5)},
        )
        recorder = self.make_recorder(proxy)
        self.assertTrue(recorder.maybe_poll())
        self.clock.now = 104.9
        self.assertFalse(recorder.maybe_poll())
        self.assertEqual(proxy.status_calls, 1)

        proxy.status = STATUS.pack(1, 176, 256, 3, 5, 8)
        proxy.samples.update({6: packed_sample(sequence=6), 7: packed_sample(sequence=7)})
        self.clock.now = 105.0
        self.assertTrue(recorder.maybe_poll())
        self.assertEqual(proxy.sample_calls, [5, 6, 7])

    def test_ring_overwrite_advances_to_new_oldest_sequence(self):
        proxy = FakeProxy(
            STATUS.pack(1, 176, 2, 2, 20, 22),
            {20: packed_sample(sequence=20), 21: packed_sample(sequence=21)},
        )
        recorder = self.make_recorder(proxy)
        recorder._next_sequence = 7
        self.assertTrue(recorder.maybe_poll())
        self.assertEqual(proxy.sample_calls, [20, 21])

    def test_missing_sample_is_retried_and_does_not_skip_sequence(self):
        proxy = FakeProxy(STATUS.pack(1, 176, 256, 1, 9, 10), {9: None})
        recorder = self.make_recorder(proxy)
        self.assertTrue(recorder.maybe_poll())
        self.assertEqual(recorder.next_sequence, 9)
        self.assertFalse(self.log_path.exists())

    def test_proxy_error_is_recorded_without_advancing_sequence(self):
        proxy = FakeProxy(RuntimeError("usb link lost"), {})
        recorder = self.make_recorder(proxy)
        recorder._next_sequence = 40
        self.assertFalse(recorder.maybe_poll())
        self.assertEqual(recorder.next_sequence, 40)
        published = json.loads(self.status_path.read_text())
        self.assertEqual(published["state"], "unavailable")
        self.assertIn("usb link lost", published["error"])

    def test_reentrant_callback_does_not_start_a_nested_proxy_request(self):
        proxy = FakeProxy(STATUS.pack(1, 176, 256, 0, 0, 0), {})
        recorder = self.make_recorder(proxy)
        original = proxy.hv_diag_status

        def reentrant_status():
            self.assertFalse(recorder.maybe_poll(force=True))
            return original()

        proxy.hv_diag_status = reentrant_status
        self.assertTrue(recorder.maybe_poll())
        self.assertEqual(proxy.status_calls, 1)

    def test_once_cli_attaches_reads_prints_and_closes(self):
        proxy = FakeProxy(
            STATUS.pack(1, 176, 256, 1, 3, 4),
            {3: packed_sample(sequence=3, guest_pc=0x1234, host_tick_count=90)},
        )

        class FakeDevice:
            def __init__(self):
                self.closed = False

            def close(self):
                self.closed = True

        interface = type("FakeInterface", (), {"dev": FakeDevice()})()
        output = io.StringIO()
        with mock.patch.object(
            hang_telemetry, "connect_proxy", return_value=(interface, proxy)
        ), mock.patch("sys.stdout", output):
            result = hang_telemetry.main([
                "--once",
                "--unsafe-direct-attach",
                "--device", "fake",
                "--jsonl", str(self.log_path),
                "--status", str(self.status_path),
            ])

        self.assertEqual(result, 0)
        self.assertTrue(interface.dev.closed)
        self.assertIn("seq=3", output.getvalue())
        self.assertIn("pc=0x0000000000001234", output.getvalue())

    def test_cli_refuses_second_proxy_owner_without_explicit_unsafe_flag(self):
        error = io.StringIO()
        with mock.patch.object(hang_telemetry, "connect_proxy") as connect, \
             mock.patch("sys.stderr", error):
            result = hang_telemetry.main([
                "--once",
                "--device", "fake",
                "--jsonl", str(self.log_path),
                "--status", str(self.status_path),
            ])

        self.assertEqual(result, 2)
        connect.assert_not_called()
        self.assertIn("single-owner", error.getvalue())


if __name__ == "__main__":
    unittest.main()
