"""Historical Asahi shim with the capture-only bridged bootstrap policy."""

import os
from pathlib import Path
import subprocess
import sys

from tools.agx_capture_bootstrap import install_bootstrap_override
from tools.agx_schema_compat import (
    install_historical_renderer_schema_compatibility,
    install_start3d_helper_cfg_compatibility,
    install_tiling_helper_cfg_default,
    install_work_command_ta_padding_compatibility,
)


def write_complete_attachment_page(cmdbuf, bos, output):
    """Atomically persist the sole fully pulled color attachment BO."""

    count = getattr(cmdbuf, "attachment_count", None)
    attachments = getattr(cmdbuf, "attachments", None)
    if count != 1 or not isinstance(attachments, (list, tuple)) or len(attachments) < 1:
        raise RuntimeError("capture requires exactly one attachment")
    attachment = attachments[0]
    pointer = getattr(attachment, "pointer", None)
    size = getattr(attachment, "size", None)
    kind = getattr(attachment, "type", None)
    if (
        kind != 0
        or isinstance(pointer, bool)
        or not isinstance(pointer, int)
        or pointer <= 0
        or isinstance(size, bool)
        or not isinstance(size, int)
        or size <= 0
        or size > 16 * 1024 * 1024
    ):
        raise RuntimeError("capture attachment metadata is invalid")
    matches = [
        obj for obj in bos.values()
        if getattr(obj, "_addr", None) == pointer
        and getattr(obj, "_size", None) == size
    ]
    if len(matches) != 1:
        raise RuntimeError("capture attachment BO is missing or ambiguous")
    data = bytes(getattr(matches[0], "val", b""))
    if len(data) != size:
        raise RuntimeError("capture attachment bytes are incomplete")

    output = Path(output)
    temporary = output.with_name(output.name + ".tmp")
    if output.exists() or temporary.exists():
        raise RuntimeError("capture attachment destination already exists")
    try:
        with temporary.open("xb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(output)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise


install_bootstrap_override()

from m1n1.agx import render as capture_render  # noqa: E402
from m1n1.agx.shim import Shim as HistoricalShim  # noqa: E402
from m1n1.constructutils import Ver  # noqa: E402
from m1n1.fw.agx.microsequence import Start3DStruct1  # noqa: E402


# The pinned renderer predates the schema rename in b50e29b and still assigns
# ``unk_40 = 0``.  Construct builds from mapping keys rather than Python
# properties, so keep the immutable m1n1 source/artifact pin intact and rename
# only the capture schema key while preserving its codec, offset and value.
install_historical_renderer_schema_compatibility(capture_render, Start3DStruct1)


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
        submit_request = getattr(self.submit, "_ioctl").value
        if self._capture_is_producer and request == submit_request and result == 0:
            target = os.environ.get("AGX_CAPTURE_FULL_ATTACHMENT")
            if not target or not self.pull_buffers:
                raise RuntimeError("complete attachment capture is not configured")
            from m1n1.agx.uapi import drm_asahi_cmdbuf_t, drm_asahi_submit_t

            args = drm_asahi_submit_t.parse(
                self.read_buf(p_arg, drm_asahi_submit_t.sizeof())
            )
            cmdbuf = drm_asahi_cmdbuf_t.parse(
                self.read_buf(args.cmdbuf, drm_asahi_cmdbuf_t.sizeof())
            )
            write_complete_attachment_page(cmdbuf, self.bos, target)
        return result

    def init_agx(self):
        from m1n1.setup import u

        Ver.set_version(u)
        if not Ver.check("V == V13_5 && G == G13"):
            raise RuntimeError("capture requires the pinned J313 V13_5/G13 layout")
        return super().init_agx()
