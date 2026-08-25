"""Bounded m1n1 adapter for the J313 AGX G1Q barrier experiment."""

import hashlib
from pathlib import Path
from types import SimpleNamespace
import sys
import time

from tools.agx_m1n1_backend import M1n1AgxBackend
from tools.agx_queue_gate import (
    COMPLETION_DEADLINE_S,
    CONTEXT_ID,
    PAGE_SIZE,
    QUEUE_INDEX,
    QueueGateError,
    validate_completion,
)


GATE_VA = 0x1600000000
GUARD_BEFORE_VA = GATE_VA - PAGE_SIZE
GUARD_AFTER_VA = GATE_VA + PAGE_SIZE
CANARY_SEED = b"G1Q-CANARY-v1!\0"
STAMP_VALUE = 0x51000000
QUEUE_UUID = 0x3D003F
ROOT = Path(__file__).resolve().parents[1]
PROXYCLIENT = ROOT / "m1n1_windows" / "proxyclient"


class QueueBackendError(RuntimeError):
    """The bounded queue adapter cannot preserve its isolation contract."""


def _default_queue_types():
    if str(PROXYCLIENT) not in sys.path:
        sys.path.insert(0, str(PROXYCLIENT))
    from m1n1.agx.context import GPU3DWorkQueue
    from m1n1.fw.agx.cmdqueue import GPUContextData, JobList, WorkCommandBarrier
    from m1n1.fw.agx.microsequence import StampCounter

    return SimpleNamespace(
        GPUContextData=GPUContextData,
        JobList=JobList,
        GPU3DWorkQueue=GPU3DWorkQueue,
        StampCounter=StampCounter,
        WorkCommandBarrier=WorkCommandBarrier,
    )


class M1n1AgxQueueBackend:
    """Compose the proven G1 lifecycle with one isolated G1Q UAT context."""

    def __init__(self, u, *, lifecycle=None, queue_types=None, clock=time.monotonic):
        self.u = u
        self.lifecycle = lifecycle or M1n1AgxBackend(u)
        self.queue_types = queue_types
        self.clock = clock
        self.agx = None
        self._context_configured = False
        self._ttbr0 = None
        self._canary_pa = None
        self._canary = None
        self._mapping = None
        self._queue = None
        self._stamp = None
        self._event = None
        self._barrier = None
        self._last_queue_evidence = None

    def prepare(self, contract) -> None:
        self.lifecycle.prepare(contract)
        self.agx = self.lifecycle.agx

    def start(self) -> None:
        self.lifecycle.start()
        self.agx = self.lifecycle.agx

    def heartbeat(self) -> dict:
        return self.lifecycle.heartbeat()

    @staticmethod
    def _translation_is(ranges, address) -> bool:
        return ranges == [(address, PAGE_SIZE)]

    @staticmethod
    def _translation_is_unmapped(ranges) -> bool:
        return ranges == [(None, PAGE_SIZE)]

    def configure_context(self, context_id: int) -> None:
        if context_id != CONTEXT_ID:
            raise QueueBackendError("G1Q requires only context 63")
        if self._context_configured:
            raise QueueBackendError("G1Q context is already configured")
        if self.agx is None:
            raise QueueBackendError("AGX lifecycle is not started")
        uat = self.agx.uat
        if int(self.agx.PAGE_SIZE) != PAGE_SIZE or int(uat.PAGE_SIZE) != PAGE_SIZE:
            raise QueueBackendError("G1Q requires UAT page size 0x4000")

        ttbr0 = self.u.memalign(PAGE_SIZE, PAGE_SIZE)
        self.agx.p.memset32(ttbr0, 0, PAGE_SIZE)
        uat.bind_context(CONTEXT_ID, ttbr0)

        canary_pa = self.u.memalign(PAGE_SIZE, PAGE_SIZE)
        canary = (
            CANARY_SEED * ((PAGE_SIZE + len(CANARY_SEED) - 1) // len(CANARY_SEED))
        )[:PAGE_SIZE]
        self.u.iface.writemem(canary_pa, canary)
        uat.iomap_at(CONTEXT_ID, GATE_VA, canary_pa, PAGE_SIZE)
        uat.flush_dirty()

        mapped = uat.iotranslate(CONTEXT_ID, GATE_VA, PAGE_SIZE)
        guard_before = uat.iotranslate(CONTEXT_ID, GUARD_BEFORE_VA, PAGE_SIZE)
        guard_after = uat.iotranslate(CONTEXT_ID, GUARD_AFTER_VA, PAGE_SIZE)
        if not self._translation_is(mapped, canary_pa):
            raise QueueBackendError("canary mapping translation mismatch")
        if not self._translation_is_unmapped(guard_before):
            raise QueueBackendError("guard before canary is mapped")
        if not self._translation_is_unmapped(guard_after):
            raise QueueBackendError("guard after canary is mapped")

        self._ttbr0 = ttbr0
        self._canary_pa = canary_pa
        self._canary = canary
        self._mapping = {
            "context_id": CONTEXT_ID,
            "va": GATE_VA,
            "pa": canary_pa,
            "size": PAGE_SIZE,
        }
        self._context_configured = True

    def mapping_evidence(self) -> dict:
        if not self._context_configured:
            raise QueueBackendError("G1Q context is not configured")
        before = self.agx.uat.iotranslate(
            CONTEXT_ID, GUARD_BEFORE_VA, PAGE_SIZE
        )
        after = self.agx.uat.iotranslate(
            CONTEXT_ID, GUARD_AFTER_VA, PAGE_SIZE
        )
        guards_unmapped = (
            self._translation_is_unmapped(before)
            and self._translation_is_unmapped(after)
        )
        return {
            "context_id": CONTEXT_ID,
            "page_size": PAGE_SIZE,
            "mapping": dict(self._mapping),
            "declared_mapping_count": 1,
            "unexpected_mappings": [],
            "guard_before_va": GUARD_BEFORE_VA,
            "guard_after_va": GUARD_AFTER_VA,
            "guards_unmapped": guards_unmapped,
            "canary_sha256": hashlib.sha256(self._canary).hexdigest(),
        }

    def _types(self):
        if self.queue_types is None:
            self.queue_types = _default_queue_types()
        return self.queue_types

    def _create_queue(self) -> None:
        types = self._types()
        scheduler = self.agx.kobj.new(types.GPUContextData).push()
        job_list = self.agx.kshared.new(types.JobList)
        job_list.first_job = 0
        job_list.last_head = job_list._addr
        job_list.unkptr_10 = 0
        job_list.push()
        queue = types.GPU3DWorkQueue(self.agx, scheduler, job_list)
        queue.info.uuid = QUEUE_UUID
        queue.info.push()

        stamp = self.agx.kshared.new(types.StampCounter, name="G1Q stamp")
        stamp.value = STAMP_VALUE
        stamp.push()
        event = self.agx.event_mgr.allocate_event()
        if event.id != 0:
            raise QueueBackendError("G1Q requires deterministic event ID 0")
        barrier = self.agx.kobj.new(
            types.WorkCommandBarrier,
            track=False,
            align=0x20,
        )
        barrier.stamp = stamp
        barrier.wait_value = STAMP_VALUE
        barrier.stamp_self = STAMP_VALUE
        barrier.event = event.id
        barrier.uuid = QUEUE_UUID

        self._queue = queue
        self._stamp = stamp
        self._event = event
        self._barrier = barrier

    def _canary_sha256(self) -> str:
        data = self.u.iface.readmem(self._canary_pa, PAGE_SIZE)
        if len(data) != PAGE_SIZE:
            raise QueueBackendError("canary read returned the wrong size")
        return hashlib.sha256(data).hexdigest()

    def submit_barrier(self, queue_index: int, timeout_s: float) -> dict:
        if queue_index != QUEUE_INDEX:
            raise QueueBackendError("G1Q requires queue index 1")
        if (isinstance(timeout_s, bool)
                or not isinstance(timeout_s, (int, float))
                or float(timeout_s) != COMPLETION_DEADLINE_S):
            raise QueueBackendError("G1Q requires a fixed 0.5 second deadline")
        if not self._context_configured:
            raise QueueBackendError("G1Q context is not configured")
        if self._queue is not None:
            raise QueueBackendError("G1Q barrier was already submitted")

        self._create_queue()
        queue = self._queue
        event = self._event
        producer_before = int(queue.wptr)
        consumer_before = int(queue.pmap.GPU_DONEPTR.val)
        event_before = int(self.agx.event_mgr.event_count)
        stamp_before = int(self._stamp.value)
        canary_before = self._canary_sha256()

        queue.submit(self._barrier)
        producer_after = int(queue.wptr)
        if producer_after <= producer_before:
            raise QueueBackendError("queue producer ring wrap is forbidden")
        if producer_after - producer_before != 1:
            raise QueueBackendError("queue producer must advance exactly once")

        self.agx.ch.queue[QUEUE_INDEX].q_3D.run(queue, event.id)
        started = self.clock()
        while self.clock() - started < float(timeout_s):
            self.agx.asc.work()
            self.agx.poll_channels()
            if event.fired:
                break

        elapsed = self.clock() - started
        consumer_after = int(queue.pmap.GPU_DONEPTR.val)
        event_after = int(self.agx.event_mgr.event_count)
        matching_events = event_after - event_before if event.fired else 0
        canary_after = self._canary_sha256()
        mapping = self.mapping_evidence()
        receipt = {
            "context_id": CONTEXT_ID,
            "page_size": PAGE_SIZE,
            "queue_index": QUEUE_INDEX,
            "queue_type": "3D",
            "submitted_commands": len(queue.submitted) if hasattr(queue, "submitted") else 1,
            "producer_before": producer_before,
            "producer_after": producer_after,
            "consumer_before": consumer_before,
            "consumer_after": consumer_after,
            "event_id": int(event.id),
            "event_count_before": event_before,
            "event_count_after": event_after,
            "matching_event_count": matching_events,
            "stamp_before": stamp_before,
            "stamp_after": int(self._stamp.value),
            "elapsed_s": float(elapsed),
            "deadline_s": float(timeout_s),
            "canary_sha256_before": canary_before,
            "canary_sha256_after": canary_after,
            "guards_unmapped": mapping["guards_unmapped"],
            "declared_mapping_count": mapping["declared_mapping_count"],
            "unexpected_mappings": mapping["unexpected_mappings"],
        }
        self._last_queue_evidence = dict(receipt)
        if not event.fired:
            raise QueueBackendError(
                "queue completion timeout: "
                f"producer={producer_after} consumer={consumer_after} "
                f"events={event_before}->{event_after}"
            )
        try:
            return validate_completion(receipt)
        except QueueGateError as exc:
            raise QueueBackendError(str(exc)) from exc

    def snapshot(self, reason: str) -> dict:
        snapshot = dict(self.lifecycle.snapshot(reason))
        snapshot["reason"] = reason
        snapshot["physical_fault"] = {
            "readable": False,
            "reason": "power-domain-not-qualified",
        }
        if not self._context_configured:
            snapshot["mapping"] = None
            snapshot["canary"] = None
        else:
            mapping = self.mapping_evidence()
            live_hash = self._canary_sha256()
            before_hash = (
                self._last_queue_evidence["canary_sha256_before"]
                if self._last_queue_evidence is not None
                else hashlib.sha256(self._canary).hexdigest()
            )
            snapshot["mapping"] = mapping
            snapshot["canary"] = {
                "sha256_before": before_hash,
                "sha256_live": live_hash,
                "changed": live_hash != before_hash,
            }

        if self._queue is None:
            snapshot["queue"] = None
        else:
            snapshot["queue"] = {
                "producer": int(self._queue.wptr),
                "consumer": int(self._queue.pmap.GPU_DONEPTR.val),
                "event_id": int(self._event.id),
                "event_count": int(self.agx.event_mgr.event_count),
                "event_fired": bool(self._event.fired),
                "stamp": int(self._stamp.value),
                "completion": (
                    dict(self._last_queue_evidence)
                    if self._last_queue_evidence is not None
                    else None
                ),
            }
        return snapshot

    def stop(self) -> None:
        self.lifecycle.stop()

    def reset(self) -> None:
        if self._context_configured and self.agx is not None:
            uat = self.agx.uat
            with uat.handoff.lock():
                uat.set_l0(CONTEXT_ID, 0, 0, CONTEXT_ID)
                uat.set_l0(CONTEXT_ID, 1, 0, CONTEXT_ID)
                uat.flush_dirty()
                uat.invalidate_cache()
            if self._event is not None:
                self.agx.event_mgr.free_event(self._event)

        self._context_configured = False
        self._ttbr0 = None
        self._canary_pa = None
        self._canary = None
        self._mapping = None
        self._queue = None
        self._stamp = None
        self._event = None
        self._barrier = None
        self._last_queue_evidence = None
        self.lifecycle.reset()
        self.agx = None

    def released(self) -> bool:
        return (
            self.lifecycle.released()
            and not self._context_configured
            and self._queue is None
            and self._event is None
        )
