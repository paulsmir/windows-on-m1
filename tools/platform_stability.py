#!/usr/bin/env python3
"""Deterministically classify a J313 Windows stability capture."""

from dataclasses import dataclass, field
import argparse
import json
from pathlib import Path
import re


@dataclass(frozen=True)
class CpuSnapshot:
    cpu: int
    progress: int
    timer_enabled: bool = False
    timer_expired: bool = False
    timer_queued: bool = False
    timer_lr: bool = False
    sgi_rate: int = 0
    # Equal PCs/counters can simply mean that Windows parked an idle CPU.  Set
    # this only when an independent source proves the CPU was expected to run.
    can_stall: bool = False


@dataclass(frozen=True)
class ProbeSample:
    timestamp: int
    ssh_alive: bool
    framebuffer_age: int


@dataclass(frozen=True)
class RunClassification:
    kind: str
    cpu: int | None = None
    details: dict = field(default_factory=dict)


_CPU_RECORD = re.compile(
    r"HV WATCHDOG CPU:\s+(?P<fields>.*?)(?=HV WATCHDOG CPU:|\[cpu\d+\]|$)"
)
_FIELD = re.compile(r"(?P<key>[a-z][a-z0-9_]*)=(?P<value>0x[0-9a-fA-F]+|[0-9]+)")
_BUGCHECK = re.compile(
    r"HV BUGCHECK:\s+seen_by_cpu=(?P<seen_by_cpu>\d+)\s+"
    r"code=(?P<code>0x[0-9a-fA-F]+)\s+"
    r"P1=(?P<p1>0x[0-9a-fA-F]+)\s+P2=(?P<p2>0x[0-9a-fA-F]+)\s+"
    r"P3=(?P<p3>0x[0-9a-fA-F]+)\s+P4=(?P<p4>0x[0-9a-fA-F]+)"
)


def _parse_number(value):
    return int(value, 16 if value.lower().startswith("0x") else 10)


def parse_log_events(text):
    """Extract authoritative terminal events before applying state heuristics."""
    events = []
    for match in _BUGCHECK.finditer(text):
        event = {"kind": "bugcheck"}
        event.update({name: _parse_number(match.group(name))
                      for name in ("seen_by_cpu", "code", "p1", "p2", "p3", "p4")})
        events.append(event)
    return events


def _parse_watchdog_cpu(line):
    match = _CPU_RECORD.search(line)
    if not match:
        return None
    fields = {item.group("key"): _parse_number(item.group("value"))
              for item in _FIELD.finditer(match.group("fields"))}
    required = {"cpu", "pc", "vctl", "tq", "q", "iar", "eoi", "marker"}
    if not required <= fields.keys():
        return None

    vctl = fields["vctl"]
    timer_lr = any((value & 0xffffffff) == 18
                   for key, value in fields.items() if key.startswith("lr"))
    # cntpct is a host physical clock and advances even when guest execution is
    # stuck.  Progress must therefore contain only guest-observable state.
    guest_progress = hash((fields["pc"], fields["q"], fields["iar"],
                           fields["eoi"], fields["marker"]))
    return CpuSnapshot(
        cpu=fields["cpu"],
        progress=guest_progress,
        timer_enabled=bool(vctl & 1),
        timer_expired=bool(vctl & 4),
        timer_queued=bool(fields["tq"]),
        timer_lr=timer_lr,
    )


def parse_watchdog_snapshots(text, expected_cpus=8):
    """Parse only complete snapshot generations from an m1n1 hypervisor log."""
    snapshots = []
    pending = {}
    for line in text.splitlines():
        for match in _CPU_RECORD.finditer(line):
            record = _parse_watchdog_cpu(match.group(0))
            if record is not None:
                pending[record.cpu] = record
        if "HOST CONTROL: diagnostic snapshot captured" in line:
            if len(pending) == expected_cpus:
                snapshots.append([pending[cpu] for cpu in sorted(pending)])
            pending = {}
    return snapshots


def _snapshots_by_cpu(snapshot):
    return {item.cpu: item for item in snapshot}


def classify_run(events, snapshots, probes, link_events):
    del events

    for event in link_events:
        if event.get("kind") == "bugcheck":
            return RunClassification("bugcheck", details=dict(event))

    if len(snapshots) >= 2:
        previous = _snapshots_by_cpu(snapshots[-2])
        current = _snapshots_by_cpu(snapshots[-1])
        common = sorted(previous.keys() & current.keys())

        for cpu in common:
            item = current[cpu]
            if (item.timer_enabled and item.timer_expired and
                    not item.timer_queued and not item.timer_lr):
                return RunClassification("timer_loss", cpu=cpu)

        storming = [cpu for cpu in common
                    if previous[cpu].sgi_rate > 10_000 and current[cpu].sgi_rate > 10_000]
        if storming:
            return RunClassification("sgi_storm", cpu=storming[0])

        advancing = [cpu for cpu in common
                     if current[cpu].progress != previous[cpu].progress]
        stalled = [cpu for cpu in common
                   if current[cpu].progress == previous[cpu].progress]
        proven_stalled = [cpu for cpu in stalled if current[cpu].can_stall]
        if advancing and proven_stalled:
            return RunClassification("cpu_stall", cpu=proven_stalled[0])
        if common and not advancing and all(current[cpu].can_stall for cpu in common) and any(
                event.get("complete_snapshot") for event in link_events):
            return RunClassification("guest_freeze")

    if any(event.get("kind") == "disconnect" and
           not event.get("complete_snapshot", False) for event in link_events):
        return RunClassification("transport_loss")
    if any(event.get("kind") == "boot" for event in link_events):
        return RunClassification("host_reset")

    if len(probes) >= 2 and all(probe.ssh_alive for probe in probes[-2:]):
        if probes[-1].framebuffer_age > probes[-2].framebuffer_age:
            return RunClassification("ui_pause")
        return RunClassification("healthy")

    if len(snapshots) >= 2:
        previous = _snapshots_by_cpu(snapshots[-2])
        current = _snapshots_by_cpu(snapshots[-1])
        common = previous.keys() & current.keys()
        if common and all(current[cpu].progress != previous[cpu].progress for cpu in common):
            return RunClassification("healthy")

    return RunClassification("incomplete")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected-cpus", type=int, default=8)
    args = parser.parse_args()

    text = args.log.read_text(encoding="utf-8", errors="replace")
    snapshots = parse_watchdog_snapshots(
        text,
        expected_cpus=args.expected_cpus,
    )
    link_events = parse_log_events(text)
    link_events.append({"kind": "sample", "complete_snapshot": bool(snapshots)})
    result = classify_run([], snapshots, [], link_events)
    payload = {
        "classification": result.kind,
        "cpu": result.cpu,
        "complete_snapshots": len(snapshots),
        "details": result.details,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    print(json.dumps(payload, sort_keys=True))


if __name__ == "__main__":
    main()
