#!/usr/bin/env python3
"""Decode and classify m1n1 Windows guest progress telemetry."""

from dataclasses import asdict, dataclass
import argparse
import json
import os
from pathlib import Path
import struct
import sys
import time


STATUS_STRUCT = struct.Struct("<IIIIQQ")
SAMPLE_STRUCT = struct.Struct("<18Q4I8H")
ABI_VERSION = 1
SAMPLE_SIZE = 176

U64_FIELDS = (
    "sequence", "host_fiq_count", "host_tick_count", "guest_pc", "guest_spsr",
    "nvme_sq_doorbells", "nvme_cq_doorbells", "nvme_commands", "nvme_completions",
    "nvme_irq_injects", "nvme_irq_iars", "nvme_irq_eois", "xhci_hw_irqs",
    "xhci_irq_injects", "xhci_irq_iars", "xhci_irq_eois", "fb_completed_frames",
    "fb_backpressure_skips",
)
U32_FIELDS = ("vgic_pending_lrs", "vgic_active_lrs", "vgic_occupied_lrs", "flags")
COUNTER_FIELDS = (
    "host_fiq_count", "host_tick_count", "nvme_sq_doorbells", "nvme_cq_doorbells",
    "nvme_commands", "nvme_completions", "nvme_irq_injects", "nvme_irq_iars",
    "nvme_irq_eois", "xhci_hw_irqs", "xhci_irq_injects", "xhci_irq_iars",
    "xhci_irq_eois", "fb_completed_frames", "fb_backpressure_skips",
)


class TelemetryProtocolError(ValueError):
    pass


@dataclass(frozen=True)
class TelemetryStatus:
    abi_version: int
    sample_size: int
    capacity: int
    count: int
    oldest_sequence: int
    next_sequence: int


@dataclass(frozen=True)
class TelemetrySample:
    sequence: int
    host_fiq_count: int
    host_tick_count: int
    guest_pc: int
    guest_spsr: int
    nvme_sq_doorbells: int
    nvme_cq_doorbells: int
    nvme_commands: int
    nvme_completions: int
    nvme_irq_injects: int
    nvme_irq_iars: int
    nvme_irq_eois: int
    xhci_hw_irqs: int
    xhci_irq_injects: int
    xhci_irq_iars: int
    xhci_irq_eois: int
    fb_completed_frames: int
    fb_backpressure_skips: int
    vgic_pending_lrs: int
    vgic_active_lrs: int
    vgic_occupied_lrs: int
    flags: int
    queues: tuple[tuple[int, int, int, int], tuple[int, int, int, int]]


def parse_status(payload: bytes) -> TelemetryStatus:
    if len(payload) != STATUS_STRUCT.size:
        raise TelemetryProtocolError(f"status size {len(payload)} != {STATUS_STRUCT.size}")
    status = TelemetryStatus(*STATUS_STRUCT.unpack(payload))
    if status.abi_version != ABI_VERSION:
        raise TelemetryProtocolError(f"unsupported telemetry ABI {status.abi_version}")
    if status.sample_size != SAMPLE_SIZE:
        raise TelemetryProtocolError(f"sample size {status.sample_size} != {SAMPLE_SIZE}")
    if status.count > status.capacity:
        raise TelemetryProtocolError("ring count exceeds capacity")
    if status.oldest_sequence > status.next_sequence:
        raise TelemetryProtocolError("ring sequence range is reversed")
    if status.next_sequence - status.oldest_sequence != status.count:
        raise TelemetryProtocolError("ring count does not match retained sequence range")
    return status


def parse_sample(payload: bytes) -> TelemetrySample:
    if len(payload) != SAMPLE_STRUCT.size:
        raise TelemetryProtocolError(f"sample size {len(payload)} != {SAMPLE_STRUCT.size}")
    values = SAMPLE_STRUCT.unpack(payload)
    scalars = values[:22]
    return TelemetrySample(*scalars, (tuple(values[22:26]), tuple(values[26:30])))


def delta(older: TelemetrySample, newer: TelemetrySample) -> dict[str, int]:
    changes = {}
    for field in COUNTER_FIELDS:
        before = getattr(older, field)
        after = getattr(newer, field)
        changes[field] = after - before if after >= before else after
    return changes


def classify_window(samples: list[TelemetrySample]) -> tuple[str, ...]:
    if len(samples) < 4:
        return ("insufficient-evidence",)
    window = samples[-4:]
    if any(newer.sequence != older.sequence + 1 for older, newer in zip(window, window[1:])):
        return ("insufficient-evidence",)

    changes = [delta(older, newer) for older, newer in zip(window, window[1:])]
    timer_progress = all(
        change["host_fiq_count"] > 0 and change["host_tick_count"] > 0
        for change in changes
    )
    pc_static = all(value.guest_pc == window[0].guest_pc for value in window[1:])
    total = {field: sum(change[field] for change in changes) for field in COUNTER_FIELDS}
    findings = []

    if timer_progress and not pc_static:
        findings.append("running")
    elif timer_progress:
        findings.extend(("timer-progress", "guest-pc-static"))

    if total["nvme_commands"] and not total["nvme_completions"]:
        findings.append("nvme-command-without-cqe")
    if total["nvme_completions"] and not total["nvme_irq_iars"]:
        findings.append("nvme-cqe-without-iar")
    if total["xhci_hw_irqs"] and not total["xhci_irq_iars"]:
        findings.append("xhci-hw-without-iar")
    if total["fb_backpressure_skips"]:
        findings.append("framebuffer-backpressure")

    return tuple(findings) if findings else ("insufficient-evidence",)


class TelemetryRecorder:
    """Drain the EL2 diagnostic ring without competing for the USB transport.

    ``maybe_poll`` is intended to be called by the existing proxy event handler.
    The guard is essential: a proxy reply may itself deliver another event and
    synchronously enter ``maybe_poll`` again.
    """

    def __init__(
        self,
        proxy,
        *,
        log_path="hang-telemetry.jsonl",
        status_path="hang-telemetry-status.json",
        interval=5.0,
        clock=time.monotonic,
        wall_clock=time.time,
    ):
        if interval <= 0:
            raise ValueError("telemetry interval must be positive")
        self.proxy = proxy
        self.log_path = Path(log_path)
        self.status_path = Path(status_path)
        self.interval = float(interval)
        self.clock = clock
        self.wall_clock = wall_clock
        self._last_attempt = None
        self._next_sequence = None
        self._recent = []
        self._last_poll_samples = []
        self._polling = False

    @property
    def next_sequence(self):
        return self._next_sequence

    @property
    def last_poll_samples(self):
        return tuple(self._last_poll_samples)

    def _atomic_json(self, value):
        self.status_path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.status_path.with_name(self.status_path.name + ".part")
        data = (json.dumps(value, sort_keys=True) + "\n").encode("utf-8")
        with temporary.open("wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, self.status_path)

    def _append_sample(self, value):
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        with self.log_path.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(asdict(value), sort_keys=True) + "\n")
            stream.flush()
            os.fsync(stream.fileno())

    def _publish_available(self, status):
        last_sequence = self._next_sequence - 1 if self._next_sequence is not None else None
        self._atomic_json({
            "state": "available",
            "updated_at": self.wall_clock(),
            "last_sequence": last_sequence,
            "findings": list(classify_window(self._recent)),
            "ring": {
                "capacity": status.capacity,
                "count": status.count,
                "oldest_sequence": status.oldest_sequence,
                "next_sequence": status.next_sequence,
            },
        })

    def _poll_once(self):
        self._last_poll_samples = []
        payload = self.proxy.hv_diag_status()
        if payload is None:
            raise TelemetryProtocolError("telemetry status is unavailable")
        status = parse_status(payload)
        sequence = max(
            status.oldest_sequence,
            self._next_sequence if self._next_sequence is not None else status.oldest_sequence,
        )
        while sequence < status.next_sequence:
            payload = self.proxy.hv_diag_sample(sequence)
            if payload is None:
                break
            value = parse_sample(payload)
            if value.sequence != sequence:
                raise TelemetryProtocolError(
                    f"requested sample {sequence}, received {value.sequence}"
                )
            self._append_sample(value)
            self._recent.append(value)
            self._recent = self._recent[-4:]
            self._last_poll_samples.append(value)
            sequence += 1
            self._next_sequence = sequence
        if self._next_sequence is None:
            self._next_sequence = sequence
        self._publish_available(status)

    def maybe_poll(self, *, force=False):
        if self._polling:
            return False
        now = self.clock()
        if (
            not force
            and self._last_attempt is not None
            and now - self._last_attempt < self.interval
        ):
            return False
        self._last_attempt = now
        self._polling = True
        try:
            self._poll_once()
        except Exception as error:
            try:
                self._atomic_json({
                    "state": "unavailable",
                    "updated_at": self.wall_clock(),
                    "error": f"{type(error).__name__}: {error}",
                    "last_sequence": (
                        self._next_sequence - 1 if self._next_sequence is not None else None
                    ),
                })
            except Exception:
                # Diagnostics must never take down the only framebuffer/proxy reader.
                pass
            return False
        finally:
            self._polling = False
        return True


class RemoteProxyHeap:
    """Tiny adapter for an observer attaching without ProxyUtils."""

    def __init__(self, proxy):
        self.proxy = proxy

    def malloc(self, size):
        return self.proxy.malloc(size)

    def free(self, address):
        self.proxy.free(address)


def connect_proxy(device):
    """Attach to a running m1n1 proxy without starting or interrupting its guest."""
    os.environ["M1N1DEVICE"] = device
    proxyclient = Path(__file__).resolve().parent / "m1n1_windows" / "proxyclient"
    sys.path.insert(0, str(proxyclient))
    from m1n1.proxy import M1N1Proxy, UartInterface
    from m1n1.proxyutils import bootstrap_port

    interface = UartInterface()
    proxy = M1N1Proxy(interface, debug=False)
    bootstrap_port(interface, proxy)
    proxy.heap = RemoteProxyHeap(proxy)
    return interface, proxy


def format_samples(samples):
    lines = []
    for value in samples:
        lines.append(
            f"seq={value.sequence:<6} pc=0x{value.guest_pc:016x} "
            f"tick={value.host_tick_count:<10} "
            f"nvme={value.nvme_commands}/{value.nvme_completions}/"
            f"{value.nvme_irq_iars} "
            f"xhci={value.xhci_hw_irqs}/{value.xhci_irq_iars} "
            f"fb={value.fb_completed_frames}/{value.fb_backpressure_skips}"
        )
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Read the non-invasive m1n1 Windows guest diagnostic ring"
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--once", action="store_true", help="fetch retained samples and exit")
    mode.add_argument("--follow", action="store_true", help="keep polling new samples")
    parser.add_argument(
        "--device",
        default=os.environ.get("M1N1DEVICE"),
    )
    parser.add_argument("--interval", type=float, default=5.0)
    parser.add_argument("--jsonl", type=Path, default=Path("hang-telemetry.jsonl"))
    parser.add_argument(
        "--status", type=Path, default=Path("hang-telemetry-status.json")
    )
    args = parser.parse_args(argv)
    if not args.device:
        parser.error("--device or M1N1DEVICE is required")
    if args.interval <= 0:
        parser.error("--interval must be positive")

    follow = args.follow
    recorder = None
    interface = None
    while True:
        try:
            interface, proxy = connect_proxy(args.device)
            if recorder is None:
                recorder = TelemetryRecorder(
                    proxy,
                    log_path=args.jsonl,
                    status_path=args.status,
                    interval=args.interval,
                )
            else:
                recorder.proxy = proxy

            while True:
                if not recorder.maybe_poll(force=True):
                    raise RuntimeError("telemetry request failed")
                rendered = format_samples(recorder.last_poll_samples)
                if rendered:
                    print(rendered, flush=True)
                if not follow:
                    return 0
                time.sleep(args.interval)
        except KeyboardInterrupt:
            return 130
        except Exception as error:
            print(f"telemetry link unavailable: {error}", file=sys.stderr, flush=True)
            if not follow:
                return 1
        finally:
            if interface is not None:
                try:
                    interface.dev.close()
                except Exception:
                    pass
                interface = None
        time.sleep(min(args.interval, 5.0))


if __name__ == "__main__":
    raise SystemExit(main())
