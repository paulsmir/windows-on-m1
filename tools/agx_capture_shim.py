"""Historical Asahi shim with the capture-only bridged bootstrap policy."""

import os
import subprocess
import sys

from tools.agx_capture_bootstrap import install_bootstrap_override


install_bootstrap_override()

from m1n1.agx.shim import Shim as HistoricalShim  # noqa: E402
from m1n1.constructutils import Ver  # noqa: E402


def isolate_capture_subprocess_memory():
    """Keep pre-exec fd closure out of the producer's drm-shim address space."""

    subprocess._USE_VFORK = False
    subprocess._USE_POSIX_SPAWN = False


class Shim(HistoricalShim):
    """Bind live firmware and GPU generation before historical AGX startup."""

    def __init__(self, memfd):
        super().__init__(memfd)
        expected_program = os.environ.get("AGX_CAPTURE_PROGRAM")
        current_program = os.path.realpath("/proc/self/exe")
        self._capture_is_producer = bool(
            expected_program
            and current_program == os.path.realpath(expected_program)
        )
        if not expected_program or current_program != os.path.realpath(expected_program):
            self._capture_setup = None
            return

        # AGX assembles firmware helpers at runtime.  With vfork/posix_spawn,
        # their pre-exec close sweep runs inside this process's address space;
        # drm-shim's interposed close() then removes the producer's fd_map
        # entries.  A normal fork gives those children a private map copy.
        isolate_capture_subprocess_memory()

        # Importing setup opens and bootstraps the m1n1 transport.  Do that
        # while drm-shim is still creating the device and before it exposes a
        # fake render fd to EGL; otherwise the first ioctl can resume with a
        # bootstrap-invalidated shim_fd.  Keep AGX.start() lazy in init(), as
        # the historical path expects, and pin the module for this Shim.
        from m1n1 import setup as capture_setup

        self._capture_setup = capture_setup

    def ioctl(self, fd, request, p_arg):
        sequence = getattr(self, "_capture_ioctl_sequence", 0) + 1
        self._capture_ioctl_sequence = sequence
        print(
            f"capture-ioctl-begin sequence={sequence} request={request:#x}",
            file=sys.stderr,
            flush=True,
        )
        try:
            result = super().ioctl(fd, request, p_arg)
        except BaseException as error:
            print(
                "capture-ioctl-error "
                f"sequence={sequence} request={request:#x} "
                f"type={type(error).__name__}",
                file=sys.stderr,
                flush=True,
            )
            raise
        print(
            "capture-ioctl-end "
            f"sequence={sequence} request={request:#x} result={result}",
            file=sys.stderr,
            flush=True,
        )
        return result

    def init_agx(self):
        from m1n1.setup import u

        Ver.set_version(u)
        if not Ver.check("V == V13_5 && G == G13"):
            raise RuntimeError("capture requires the pinned J313 V13_5/G13 layout")
        return super().init_agx()
