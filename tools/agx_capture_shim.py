"""Historical Asahi shim with the capture-only bridged bootstrap policy."""

from tools.agx_capture_bootstrap import install_bootstrap_override


install_bootstrap_override()

from m1n1.agx.shim import Shim  # noqa: E402,F401
