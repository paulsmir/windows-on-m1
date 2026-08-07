#!/usr/bin/env python3
"""Passively record both m1n1 USB ACM channels across target resets."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
import sys
import threading
import time
from typing import Callable, Iterable

import serial
from serial.tools import list_ports


M1N1_USB_VID = 0x1209
M1N1_USB_PID = 0x316D


class PortSelectionError(RuntimeError):
    """The connected USB devices do not identify exactly one m1n1 pair."""


@dataclass(frozen=True)
class MonitorPort:
    device: str
    serial_number: str | None
    location: str | None


@dataclass(frozen=True)
class MonitorPair:
    console: MonitorPort
    vuart: MonitorPort


def _describe_ports(ports: Iterable[object]) -> str:
    descriptions = []
    for port in sorted(ports, key=lambda value: str(value.device)):
        descriptions.append(
            f"{port.device} serial={port.serial_number!r} location={port.location!r}"
        )
    return ", ".join(descriptions) or "none"


def select_monitor_ports(
    ports: Iterable[object],
    *,
    explicit_console: str | None = None,
    explicit_vuart: str | None = None,
) -> MonitorPair:
    """Select one metadata-matched m1n1 composite device without guessing."""
    if explicit_console or explicit_vuart:
        if not explicit_console or not explicit_vuart:
            raise PortSelectionError("--console and --vuart must be supplied together")
        if explicit_console == explicit_vuart:
            raise PortSelectionError("console and vUART must be different devices")
        return MonitorPair(
            MonitorPort(explicit_console, None, None),
            MonitorPort(explicit_vuart, None, None),
        )

    candidates = [
        port
        for port in ports
        if port.vid == M1N1_USB_VID and port.pid == M1N1_USB_PID
    ]
    groups: dict[tuple[str, str], list[object]] = {}
    for port in candidates:
        if not port.serial_number or not port.location:
            continue
        groups.setdefault((port.serial_number, port.location), []).append(port)

    pairs = [group for group in groups.values() if len(group) == 2]
    if len(pairs) != 1:
        reason = "no complete metadata-matched pair" if not pairs else "multiple m1n1 pairs"
        raise PortSelectionError(f"{reason}; candidates: {_describe_ports(candidates)}")

    first, second = sorted(pairs[0], key=lambda value: str(value.device))
    return MonitorPair(
        MonitorPort(first.device, first.serial_number, first.location),
        MonitorPort(second.device, second.serial_number, second.location),
    )


def generation_directory(root: Path, number: int) -> Path:
    if number < 1:
        raise ValueError("generation number must be positive")
    return root / f"generation-{number:03d}"


def utc_timestamp() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def capture_generation(
    pair: MonitorPair,
    root: Path,
    number: int,
    *,
    serial_factory: Callable[..., object] = serial.Serial,
    timestamp: Callable[[], str] = utc_timestamp,
    emit: Callable[[str, bytes], None] | None = None,
) -> list[str]:
    """Capture one connected generation until either ACM endpoint disappears."""
    directory = generation_directory(Path(root), number)
    directory.mkdir(parents=True, exist_ok=False)
    events_path = directory / "events.log"
    event_lock = threading.Lock()
    reasons: list[str] = []

    def event(message: str) -> None:
        with event_lock, events_path.open("a", encoding="utf-8") as output:
            output.write(f"{timestamp()} {message}\n")

    opened: dict[str, object] = {}
    try:
        opened["console"] = serial_factory(pair.console.device, baudrate=115200, timeout=0.25)
        opened["vuart"] = serial_factory(pair.vuart.device, baudrate=115200, timeout=0.25)
    except Exception as exc:
        for device in opened.values():
            device.close()
        event(f"open failed: {type(exc).__name__}: {exc}")
        raise

    event(f"opened console={pair.console.device} vuart={pair.vuart.device}")

    def reader(role: str, device: object) -> None:
        raw_path = directory / f"{role}.raw"
        tlog_path = directory / f"{role}.tlog"
        try:
            with raw_path.open("xb") as raw, tlog_path.open("x", encoding="utf-8") as tlog:
                while True:
                    chunk = device.read(4096)
                    if not chunk:
                        continue
                    raw.write(chunk)
                    raw.flush()
                    text = chunk.decode("utf-8", errors="replace")
                    tlog.write(f"{timestamp()} {text}")
                    if not text.endswith("\n"):
                        tlog.write("\n")
                    tlog.flush()
                    if emit:
                        emit(role, chunk)
        except Exception as exc:
            reason = f"disconnect {role}: {type(exc).__name__}: {exc}"
            with event_lock:
                reasons.append(reason)
            event(reason)
            # Both interfaces belong to one composite USB device. Closing the
            # pair makes the sibling reader leave promptly while still letting
            # it drain anything returned before its own disconnect.
            for endpoint in opened.values():
                endpoint.close()

    threads = [
        threading.Thread(target=reader, args=(role, device), name=f"monitor-{role}", daemon=True)
        for role, device in opened.items()
    ]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    for device in opened.values():
        device.close()
    event("generation closed")
    return reasons


def _live_output(role: str, chunk: bytes) -> None:
    text = chunk.decode("utf-8", errors="replace")
    for line in text.splitlines(keepends=True):
        sys.stdout.write(f"[{role}] {line}")
    if text and not text.endswith(("\n", "\r")):
        sys.stdout.write("\n")
    sys.stdout.flush()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--console")
    parser.add_argument("--vuart")
    parser.add_argument("--output", type=Path, default=Path("standalone-monitor-logs"))
    parser.add_argument("--once", action="store_true")
    args = parser.parse_args(argv)
    if bool(args.console) != bool(args.vuart):
        parser.error("--console and --vuart must be supplied together")

    generation = 1
    try:
        while True:
            try:
                pair = select_monitor_ports(
                    list_ports.comports(),
                    explicit_console=args.console,
                    explicit_vuart=args.vuart,
                )
                print(
                    f"generation {generation}: console={pair.console.device} "
                    f"vuart={pair.vuart.device}",
                    flush=True,
                )
                capture_generation(pair, args.output, generation, emit=_live_output)
                if args.once:
                    return 0
                generation += 1
            except PortSelectionError as exc:
                print(f"waiting for m1n1 USB monitor: {exc}", file=sys.stderr, flush=True)
            except (OSError, serial.SerialException) as exc:
                print(f"USB monitor unavailable: {exc}", file=sys.stderr, flush=True)
            time.sleep(1.0)
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
