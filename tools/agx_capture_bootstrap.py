"""Capture-only m1n1 bootstrap policy for bridged historical Mesa clients."""

from __future__ import annotations

import math
import os


MIN_TIMEOUT = 0.15
MAX_TIMEOUT = 10.0
DEFAULT_TIMEOUT = 3.0


def timeout_from_environment() -> float:
    raw = os.environ.get("M1N1_BOOTSTRAP_TIMEOUT", str(DEFAULT_TIMEOUT))
    try:
        value = float(raw)
    except ValueError as exc:
        raise ValueError("M1N1_BOOTSTRAP_TIMEOUT must be a number") from exc
    if not math.isfinite(value) or not MIN_TIMEOUT <= value <= MAX_TIMEOUT:
        raise ValueError(
            f"M1N1_BOOTSTRAP_TIMEOUT must be between {MIN_TIMEOUT} and {MAX_TIMEOUT}"
        )
    return value


def bootstrap_port(iface, proxy, *, initial_timeout: float) -> None:
    """Run upstream bootstrap semantics with an explicit first-reply budget."""
    from m1n1.proxy import IODEV, ProxyCommandError, UartTimeout

    if not math.isfinite(initial_timeout) or not MIN_TIMEOUT <= initial_timeout <= MAX_TIMEOUT:
        raise ValueError("invalid initial bootstrap timeout")

    previous_timeout = iface.dev.timeout
    iface.dev.timeout = initial_timeout
    try:
        try:
            do_baud = proxy.iodev_whoami() == IODEV.UART
        except ProxyCommandError:
            do_baud = True
        except UartTimeout:
            iface.dev.baudrate = 1500000
            do_baud = False

        if do_baud:
            try:
                chip_id = proxy.get_chipid()
                if chip_id in (
                    0x8960, 0x7000, 0x7001, 0x8000, 0x8001,
                    0x8003, 0x8010, 0x8011, 0x8015,
                ):
                    proxy.cpufreq_init()
            except ProxyCommandError:
                pass

            try:
                iface.nop()
                proxy.set_baud(1500000)
            except UartTimeout:
                iface.dev.baudrate = 1500000

        iface.nop()
    finally:
        iface.dev.timeout = previous_timeout


def install_bootstrap_override() -> float:
    """Patch only this process before the historical shim imports setup.py."""
    import m1n1.proxyutils as proxyutils

    timeout = timeout_from_environment()

    def capture_bootstrap(iface, proxy):
        return bootstrap_port(iface, proxy, initial_timeout=timeout)

    proxyutils.bootstrap_port = capture_bootstrap
    return timeout
