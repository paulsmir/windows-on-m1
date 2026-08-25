"""Historical Asahi shim with the capture-only bridged bootstrap policy."""

import sys

from tools.agx_capture_bootstrap import install_bootstrap_override


install_bootstrap_override()

from m1n1.agx.shim import Shim as HistoricalShim  # noqa: E402
from m1n1.constructutils import Ver  # noqa: E402


class Shim(HistoricalShim):
    """Bind live firmware and GPU generation before historical AGX startup."""

    def __init__(self, memfd):
        super().__init__(memfd)
        # Historical Mesa lazily initializes from its first DRM ioctl.  That
        # reenters Python while the fake render fd is live, and the bootstrap
        # can unregister the fd before the enclosing C handler resumes.
        # Complete bootstrap while drm-shim is still creating the device and
        # before it exposes a fake render fd to EGL.
        self.init()

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
