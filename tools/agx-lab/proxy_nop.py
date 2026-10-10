"""Check that the m1n1 proxy answers (the J313 is at "Running proxy").

Usage: proxy_nop.py <checkout with m1n1_windows> <proxy serial device>
Run it under scripts/air-serial-guard.py so no launcher shares the port.
Prints PROXY_NOP_PASS on success.
"""
import sys
from pathlib import Path

repo, port = sys.argv[1], sys.argv[2]
sys.path.insert(0, str(Path(repo) / "m1n1_windows" / "proxyclient"))
from m1n1.proxy import M1N1Proxy, UartInterface  # noqa: E402

interface = UartInterface(port)
try:
    M1N1Proxy(interface).nop()
    print("PROXY_NOP_PASS", flush=True)
finally:
    interface.dev.close()
