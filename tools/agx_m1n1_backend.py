"""Thin m1n1 backend for the bounded J313 AGX firmware-only gate."""

from collections.abc import Callable
from dataclasses import replace

from tools.agx_contract import AgxContract, canonical_bytes


class BackendError(RuntimeError):
    """The m1n1 AGX lifecycle cannot safely continue or release ownership."""


def _default_live_contract_reader(contract: AgxContract) -> AgxContract:
    from tools.agx_inventory import extract_contract
    from tools.agx_live_inventory import capture_raw

    return extract_contract(
        capture_raw(),
        {
            "root_commit": contract.source.root_commit,
            "m1n1_commit": contract.source.m1n1_commit,
            "mu_commit": contract.source.mu_commit,
        },
    )


def _default_agx_factory(u):
    from m1n1.agx import AGX

    return AGX(u)


def _default_version_setter(u) -> None:
    from m1n1.constructutils import Ver

    Ver.set_version(u)


def _register_value(value):
    if value is None:
        return None
    if hasattr(value, "val"):
        return int(value.val)
    if hasattr(value, "value"):
        return int(value.value)
    try:
        return int(value)
    except (TypeError, ValueError):
        return str(value)


def _live_identity_bytes(contract: AgxContract) -> bytes:
    """Canonicalize ADT enumeration order without weakening resource checks."""

    return canonical_bytes(
        replace(contract, nodes=tuple(sorted(contract.nodes)))
    )


class M1n1AgxBackend:
    """Own AGX only inside a bounded gate cycle; never submit render work."""

    CLOCK_PATHS = ("/arm-io/gfx-asc", "/arm-io/sgx")

    def __init__(
        self,
        u,
        *,
        live_contract_reader: Callable[[AgxContract], AgxContract] | None = None,
        agx_factory: Callable | None = None,
        version_setter: Callable | None = None,
        heartbeat_attempts: int = 256,
    ):
        if (
            isinstance(heartbeat_attempts, bool)
            or not isinstance(heartbeat_attempts, int)
            or heartbeat_attempts <= 0
        ):
            raise BackendError("heartbeat_attempts must be a positive integer")
        self.u = u
        self.live_contract_reader = (
            live_contract_reader or _default_live_contract_reader
        )
        self.agx_factory = agx_factory or _default_agx_factory
        self.version_setter = version_setter or _default_version_setter
        self.heartbeat_attempts = heartbeat_attempts
        self.agx = None
        self._prepared = False
        self._started = False
        self._clocked = False
        self._released = True
        self._management_pongs = 0

    def _install_progress_tracker(self) -> None:
        mgmt = self.agx.asc.mgmt
        original = mgmt.msghandler.get(4)
        if original is None:
            raise BackendError("AGX management endpoint has no Pong handler")

        def tracked_pong(message):
            result = original(message)
            self._management_pongs += 1
            return result

        mgmt.msghandler[4] = tracked_pong

    def prepare(self, contract: AgxContract) -> None:
        if not self._released or self._prepared or self.agx is not None:
            raise BackendError("AGX backend already owns resources")

        live_contract = self.live_contract_reader(contract)
        if _live_identity_bytes(live_contract) != _live_identity_bytes(contract):
            raise BackendError("live AGX resources do not match reviewed contract")

        # m1n1's AGX Construct layouts are selected from the live firmware and
        # SoC generation.  The upstream AGX experiments establish this global
        # schema before constructing AGX; leaving the default at V12_3 corrupts
        # every versioned initdata layout on J313 V13_5.
        self.version_setter(self.u)
        self._released = False
        # Once a clock-enable request is attempted, only a proven hardware
        # reset may claim release.  A transport error can occur after the
        # request reached the target, so treating it as an untouched state is
        # unsafe.
        self._clocked = True
        for path in self.CLOCK_PATHS:
            self.u.proxy.pmgr_adt_clocks_enable(path)
        self.agx = self.agx_factory(self.u)
        self._install_progress_tracker()
        self._prepared = True

    def start(self) -> None:
        if not self._prepared or self.agx is None or self._started:
            raise BackendError("AGX backend is not prepared for start")
        # start() is not transactional: ASC and endpoints are live before
        # initdata construction completes.  Mark ownership first so an
        # exception can never be mistaken for a released device.
        self._started = True
        self.agx.start()

    def heartbeat(self) -> dict:
        if not self._started or self.agx is None:
            raise BackendError("AGX firmware is not started")

        before_pongs = self._management_pongs
        before_events = int(self.agx.event_mgr.event_count)
        self.agx.asc.mgmt.ping()
        for _ in range(self.heartbeat_attempts):
            self.agx.asc.work()
            if self._management_pongs > before_pongs:
                break
        if self._management_pongs <= before_pongs:
            raise BackendError("AGX heartbeat observed no management progress")

        event_count = int(self.agx.event_mgr.event_count)
        return {
            "progress": True,
            "management_pongs": self._management_pongs,
            "event_count": event_count,
            "event_delta": event_count - before_events,
        }

    def _firmware_snapshot(self) -> dict:
        mgmt = self.agx.asc.mgmt
        status = getattr(self.agx, "fw_status", None)
        firmware_status = {}
        for name in ("halted", "halt_count", "resume"):
            if status is not None and hasattr(status, name):
                firmware_status[name] = _register_value(getattr(status, name))
        return {
            "asc_running": bool(self.agx.asc.is_running()),
            "iop_power_state": int(mgmt.iop_power_state),
            "ap_power_state": int(mgmt.ap_power_state),
            "status": firmware_status,
        }

    def _fault_snapshot(self) -> dict:
        # The physical SGX fault register is not readable until the render
        # power-control path is enabled.  G1 deliberately owns firmware only,
        # so read the versioned firmware fault record from shared memory.
        region = self.agx.initdata.regionC
        region.pull()
        fault = region.fault_info
        fields = ("unk_0", "unk_4", "queue_uuid", "unk_c", "unk_10", "unk_14")
        return {
            "source": "firmware-shared-memory",
            **{name: int(getattr(fault, name)) for name in fields},
        }

    def _uat_snapshot(self) -> dict:
        uat = self.agx.uat
        dirty_ranges = {
            str(context): [[int(base), int(size)] for base, size in ranges]
            for context, ranges in sorted(uat.dirty_ranges.items())
        }
        return {
            "initialized": bool(uat.initialized),
            "gpu_region": _register_value(getattr(uat, "gpu_region", None)),
            "cached_table_roots": sorted(int(value) for value in uat.pt_cache),
            "dirty_table_roots": sorted(int(value) for value in uat.dirty),
            "dirty_ranges": dirty_ranges,
        }

    def snapshot(self, reason: str) -> dict:
        if self.agx is None:
            return {
                "reason": reason,
                "firmware": None,
                "sgx_irqs": [],
                "fault": None,
                "uat_mappings": None,
            }
        return {
            "reason": reason,
            "firmware": self._firmware_snapshot(),
            "sgx_irqs": [int(value) for value in self.agx.get_irqs()],
            "fault": self._fault_snapshot(),
            "uat_mappings": self._uat_snapshot(),
        }

    def stop(self) -> None:
        if self.agx is None:
            raise BackendError("AGX backend has no owned instance to stop")
        if self._started:
            self.agx.stop()
            self._started = False

    def reset(self) -> None:
        if self._started:
            raise BackendError("AGX firmware must be stopped before reset")
        if self.agx is None:
            if self._clocked:
                raise BackendError("AGX construction failed after clocks enabled")
            self._prepared = False
            self._released = True
            return

        uat = self.agx.uat
        with uat.handoff.lock():
            uat.set_l0(0, 0, 0)
            uat.set_l0(0, 1, 0)
            uat.flush_dirty()
            uat.invalidate_cache()

        self.agx = None
        self._prepared = False
        self._clocked = False
        self._released = True

    def released(self) -> bool:
        return self._released and not self._started and self.agx is None
