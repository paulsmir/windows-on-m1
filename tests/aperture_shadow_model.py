"""Executable T7f design sketch, NOT production code or a Windows emulator.

One native group, four independent system byte arrays, synchronous transitions.
Caller-supplied reservation, exclusive CPU leases, held source pages, joined GPU
jobs and broker acknowledgments are ASSUMPTIONS. The current KMD does not supply
this protocol. No physical allocator, actual G4 admission, WDK ordering, cache
maintenance, partial unmap, reset or cross-process synchronization is modeled.
"""

PAGE = 4096
NATIVE = 4 * PAGE


class ShadowEpoch:
    def __init__(self, pages):
        if len(pages) != 4 or any(len(page) != PAGE for page in pages):
            raise ValueError("one complete native group required")
        self.pages = pages
        self.shadow = None
        self.phase = "system"
        self.cpu_leases = 0
        self.source_held = False
        self.aliases = set()
        self.events = []

    def require(self, *phases):
        if self.phase not in phases:
            raise RuntimeError("invalid ownership transition: " + self.phase)

    def prepare(self, reserved_bytes):
        self.require("system")
        if self.cpu_leases or reserved_bytes < NATIVE:
            return False
        self.shadow = bytearray(NATIVE)
        self.source_held = True
        self.phase = "prepared"
        self.events.append("reserve")
        return True

    def copy_in(self):
        self.require("prepared")
        # This executes after earlier paging records, not during preparation.
        self.shadow[:] = b"".join(self.pages)
        self.phase = "copied"
        self.events.append("copy-in")

    def publish(self, alias):
        self.require("copied", "published", "gpu")
        self.aliases.add(alias)
        if self.phase == "copied":
            self.phase = "published"
        self.events.append("publish")

    def signal_ready(self):
        self.require("published")
        self.phase = "gpu"
        self.events.append("ready-fence")

    def gpu_read(self, alias):
        self.require("gpu")
        if alias not in self.aliases:
            raise RuntimeError("no published alias")
        return bytes(self.shadow)

    def gpu_write(self, alias, offset, data):
        self.gpu_read(alias)
        if offset < 0 or offset + len(data) > NATIVE:
            raise ValueError("out-of-group write")
        self.shadow[offset:offset + len(data)] = data
        self.events.append("gpu-write")

    def retire(self, revoke_ack):
        self.require("gpu")
        if not revoke_ack:
            self.phase = "uncertain"
            raise RuntimeError("retain both backings after uncertain revoke")
        self.aliases.clear()
        self.events.append("revoke-ack")
        for i, page in enumerate(self.pages):
            page[:] = self.shadow[i * PAGE:(i + 1) * PAGE]
        self.events.append("copy-out")
        self.events.append("retire-fence")
        self.shadow = None
        self.source_held = False
        self.phase = "system"
        self.events.append("release")

    def cpu_begin(self):
        self.require("system", "gpu")
        if self.phase == "gpu":
            # Assumes the proposed bridge joins users and receives an ACK.
            self.retire(revoke_ack=True)
        self.cpu_leases += 1

    def cpu_end(self):
        self.require("system")
        if not self.cpu_leases:
            raise RuntimeError("unbalanced CPU lease")
        self.cpu_leases -= 1
