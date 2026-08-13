#!/usr/bin/env python3
"""Identify m1n1 proxy/vUART roles by protocol, never by USB name order."""

import argparse
import contextlib
import glob
import io
import os
import sys


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "m1n1_windows", "proxyclient"))


def select_proxy_and_vuart(paths, probe):
    proxies = [path for path in paths if probe(path)]
    if len(proxies) != 1:
        raise RuntimeError(f"expected exactly one m1n1 proxy, found {len(proxies)}")
    proxy = proxies[0]
    others = [path for path in paths if path != proxy]
    return proxy, others[0] if len(others) == 1 else None


def parse_role_output(output):
    """Extract the two device paths from a probe transcript, rejecting noise."""
    paths = [line.strip() for line in output.splitlines()
             if line.strip().startswith("/dev/cu.usbmodem")]
    if len(paths) != 2:
        raise RuntimeError(f"expected two USB modem paths, found {len(paths)}")
    return paths[0], paths[1]


def probe_proxy(path):
    from m1n1.proxy import M1N1Proxy, UartInterface
    from m1n1.proxyutils import bootstrap_port

    interface = None
    try:
        # m1n1 can emit early TTY text while probing.  It is diagnostic data,
        # not part of this tool's two-line machine-readable stdout contract.
        with contextlib.redirect_stdout(io.StringIO()), \
             contextlib.redirect_stderr(io.StringIO()):
            interface = UartInterface(path)
            proxy = M1N1Proxy(interface, debug=False)
            bootstrap_port(interface, proxy)
        return True
    except Exception:
        return False
    finally:
        if interface is not None:
            interface.dev.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("ports", nargs="*")
    args = parser.parse_args()
    paths = args.ports or sorted(glob.glob("/dev/cu.usbmodem*"))
    proxy, vuart = select_proxy_and_vuart(paths, probe_proxy)
    print(proxy)
    print(vuart or "")


if __name__ == "__main__":
    main()
