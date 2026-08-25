"""Bounded m1n1 adapter for one private J313 AGX TA-to-3D render."""

import copy
import hashlib
import json
from pathlib import Path
from types import MappingProxyType, SimpleNamespace
import sys
import tempfile
import time

from tools.agx_frame_fixture import _canonical_zip_bytes
from tools.agx_m1n1_backend import M1n1AgxBackend
from tools.agx_render_gate import (
    COMPLETION_DEADLINE_S,
    CONTEXT_ID,
    PAGE_SIZE,
    QUEUE_INDEX,
    RenderGateError,
    validate_render_completion,
)


ROOT = Path(__file__).resolve().parents[1]
PROXYCLIENT = ROOT / "m1n1_windows" / "proxyclient"
BOOTSTRAP_VA = 0x6FFFFF8000


class RenderBackendError(RuntimeError):
    """The private render adapter cannot preserve its qualification contract."""


def _default_render_types():
    if str(PROXYCLIENT) not in sys.path:
        sys.path.insert(0, str(PROXYCLIENT))
    from m1n1.agx.context import GPUContext
    from m1n1.agx.render import GPUFrame, GPURenderer

    return SimpleNamespace(
        GPUContext=GPUContext,
        GPUFrame=GPUFrame,
        GPURenderer=GPURenderer,
    )


def _plain(value):
    if isinstance(value, (dict, MappingProxyType)):
        return {key: _plain(item) for key, item in value.items()}
    if isinstance(value, (tuple, list)):
        return [_plain(item) for item in value]
    return value


def _value(register) -> int:
    if hasattr(register, "val"):
        return int(register.val)
    if hasattr(register, "value"):
        return int(register.value)
    return int(register)


class M1n1AgxRenderBackend:
    """Compose the proven AGX lifecycle with one disposable context-63 frame."""

    def __init__(
        self,
        u,
        *,
        lifecycle=None,
        render_types=None,
        clock=time.monotonic,
    ):
        self.u = u
        self.lifecycle = lifecycle or M1n1AgxBackend(u)
        self.render_types = render_types
        self.clock = clock
        self.agx = None
        self.fixture = None
        self.context = None
        self.frame = None
        self.renderer = None
        self._temporary_frame_path = None
        self._baseline_objects = set()
        self._baseline_tracked_objects = set()
        self._context_configured = False
        self._configuration_failed = False
        self._frame_cleaned = False
        self._roots_cleared = False
        self._events_freed = False
        self._mapping_evidence = None
        self._last_receipt = None
        self._last_snapshot = None
        self._output_before = None
        self._output_after = None
        self._immutable_before = None
        self._immutable_after = None

    @property
    def temporary_frame_path(self) -> Path:
        if self._temporary_frame_path is None:
            raise RenderBackendError("temporary frame has not been materialized")
        return self._temporary_frame_path

    def _types(self):
        if self.render_types is None:
            self.render_types = _default_render_types()
        return self.render_types

    def prepare(self, contract, fixture) -> None:
        if self.fixture is not None:
            raise RenderBackendError("render backend is already prepared")
        self.lifecycle.prepare(contract)
        self.agx = self.lifecycle.agx
        self.fixture = fixture

    def start(self) -> None:
        if self.fixture is None:
            raise RenderBackendError("render backend is not prepared")
        self.lifecycle.start()
        self.agx = self.lifecycle.agx

    def heartbeat(self) -> dict:
        return self.lifecycle.heartbeat()

    def _frame_zip(self) -> bytes:
        members = {
            "cmdbuf.json": json.dumps(
                _plain(self.fixture.command_buffer),
                sort_keys=True,
                separators=(",", ":"),
            ).encode(),
        }
        objects = []
        for item in self.fixture.objects:
            member = f"obj_{item.gpu_va:x}.bin"
            members[member] = bytes(item.data)
            objects.append({
                "file": member,
                "name": item.name,
                "addr": item.gpu_va,
                "size": item.size,
                "map_flags": dict(item.map_flags),
            })
        members["objects.json"] = json.dumps(
            objects, sort_keys=True, separators=(",", ":")
        ).encode()
        return _canonical_zip_bytes(members)

    def configure_context(self, context_id: int) -> None:
        if context_id != CONTEXT_ID:
            raise RenderBackendError("private render requires only context 63")
        if self._context_configured:
            raise RenderBackendError("context 63 is already configured")
        if self.agx is None:
            raise RenderBackendError("AGX lifecycle is not started")
        if (
            int(getattr(self.agx, "PAGE_SIZE", PAGE_SIZE)) != PAGE_SIZE
            or int(self.agx.uat.PAGE_SIZE) != PAGE_SIZE
        ):
            raise RenderBackendError("private render requires UAT page size 0x4000")

        try:
            self._baseline_objects = set(self.agx.all_objects)
            self._baseline_tracked_objects = set(
                getattr(self.agx, "tracked_objects", {})
            )
            types = self._types()
            self.context = types.GPUContext(self.agx)
            # Binding is not transactional.  Claim context ownership before
            # the call so reset always clears a possibly installed root.
            self._context_configured = True
            self.context.bind(CONTEXT_ID)

            handle = tempfile.NamedTemporaryFile(
                prefix="agx-g1r-", suffix=".zip", delete=False
            )
            try:
                handle.write(self._frame_zip())
                handle.flush()
            finally:
                handle.close()
            self._temporary_frame_path = Path(handle.name)

            self.frame = types.GPUFrame(
                self.context, str(self._temporary_frame_path), track=False
            )
            loaded = {
                (int(obj._addr), int(obj._size)): self._object_bytes(obj)
                for obj in self.frame.objects
            }
            expected = {
                (item.gpu_va, item.size): bytes(item.data)
                for item in self.fixture.objects
            }
            if loaded != expected:
                raise RenderBackendError(
                    "materialized frame differs from validated fixture"
                )

            self.renderer = types.GPURenderer(
                self.context, buffers=16, bm_slot=0, queue=QUEUE_INDEX
            )
            self._mapping_evidence = self._classify_mappings()
            if self._mapping_evidence["unexpected_mappings"]:
                raise RenderBackendError(
                    "unexpected mappings exist before render submission"
                )
            if not self._mapping_evidence["guards_unmapped"]:
                raise RenderBackendError(
                    "private mapping guards are not unmapped"
                )
            classes = {
                item["class"]
                for item in self._mapping_evidence["mapping_classification"]
            }
            if not {"bootstrap", "frame", "renderer"}.issubset(classes):
                raise RenderBackendError(
                    "mapping classification lacks a required class"
                )
        except Exception:
            # A failed constructor can have allocated events or installed UAT
            # mappings which are no longer discoverable.  Clean what remains,
            # but never report release until the required cold reboot.
            self._configuration_failed = True
            raise

    @staticmethod
    def _object_bytes(obj) -> bytes:
        obj.pull()
        value = obj.val
        if isinstance(value, bytes):
            data = value
        else:
            try:
                data = bytes(value)
            except TypeError as exc:
                raise RenderBackendError(
                    f"object {getattr(obj, '_name', '?')} is not byte-addressable"
                ) from exc
        size = int(obj._size)
        if len(data) != size:
            raise RenderBackendError("GPU object read returned the wrong size")
        return data

    def _classify_mappings(self) -> dict:
        frame_addresses = {item.gpu_va for item in self.fixture.objects}
        classification = []
        unexpected = []
        intervals = []
        for (context, address), obj in sorted(self.agx.all_objects.items()):
            context = int(context)
            address = int(getattr(obj, "_addr_align", address))
            size = int(getattr(obj, "_size_align", getattr(obj, "_size", 0)))
            if context == 0:
                kind = "firmware-shared"
            elif context == CONTEXT_ID and address == BOOTSTRAP_VA:
                kind = "bootstrap"
            elif context == CONTEXT_ID and address in frame_addresses:
                kind = "frame"
            elif context == CONTEXT_ID:
                kind = "renderer"
            else:
                unexpected.append({
                    "context_id": context, "gpu_va": address, "size": size
                })
                continue
            if address % PAGE_SIZE or size <= 0 or size % PAGE_SIZE:
                unexpected.append({
                    "context_id": context, "gpu_va": address, "size": size
                })
                continue
            classification.append({
                "class": kind,
                "context_id": context,
                "gpu_va": address,
                "size": size,
            })
            if context == CONTEXT_ID:
                intervals.append((address, address + size))

        guards_unmapped = self._guards_unmapped(intervals)
        return {
            "context_id": CONTEXT_ID,
            "page_size": PAGE_SIZE,
            "declared_mapping_count": len(classification),
            "mapping_classification": classification,
            "unexpected_mappings": unexpected,
            "guards_unmapped": guards_unmapped,
        }

    def _guards_unmapped(self, intervals) -> bool:
        intervals = sorted(intervals)
        for previous, current in zip(intervals, intervals[1:]):
            if current[0] < previous[1]:
                return False
        translate = getattr(self.agx.uat, "iotranslate", None)
        if translate is None:
            return True
        for start, end in intervals:
            for guard in (start - PAGE_SIZE, end):
                if any(other_start <= guard < other_end for other_start, other_end in intervals):
                    continue
                if translate(CONTEXT_ID, guard, PAGE_SIZE) != [(None, PAGE_SIZE)]:
                    return False
        return True

    def mapping_evidence(self) -> dict:
        if self._mapping_evidence is None:
            raise RenderBackendError("context 63 is not configured")
        return {
            key: [dict(item) for item in value] if isinstance(value, list) else value
            for key, value in self._mapping_evidence.items()
        }

    @staticmethod
    def _queue_pointers(queue) -> dict:
        return {
            "producer": int(queue.wptr),
            "read": _value(queue.pmap.GPU_RPTR),
            "done": _value(queue.pmap.GPU_DONEPTR),
        }

    @staticmethod
    def _stamp(stamp) -> int:
        return int(stamp.pull().value)

    def _fixture_object(self, address: int):
        for obj in self.frame.objects:
            if int(obj._addr) == address:
                return obj
        raise RenderBackendError(f"fixture object is missing at {address:#x}")

    def _immutable_sha256(self) -> str:
        digest = hashlib.sha256()
        for item in sorted(self.fixture.objects, key=lambda value: value.gpu_va):
            if item.gpu_va == self.fixture.output_gpu_va:
                continue
            obj = self._fixture_object(item.gpu_va)
            digest.update(item.gpu_va.to_bytes(8, "little"))
            digest.update(item.size.to_bytes(8, "little"))
            digest.update(self._object_bytes(obj))
        return digest.hexdigest()

    @staticmethod
    def _firmware_faults(snapshot) -> dict:
        fault = snapshot.get("fault") or {}
        numeric = {
            key: int(value)
            for key, value in fault.items()
            if key != "source" and isinstance(value, int) and not isinstance(value, bool)
        }
        return {} if numeric and not any(numeric.values()) else numeric

    def _cleanup_frame(self) -> bool:
        if self._frame_cleaned:
            return True
        errors = []
        if self.renderer is not None:
            for work in reversed(list(getattr(self.renderer, "work", []))):
                try:
                    work.free()
                except Exception as exc:
                    errors.append(str(exc))
        if self.agx is not None:
            created = [
                obj for key, obj in list(self.agx.all_objects.items())
                if key not in self._baseline_objects
            ]
            for obj in reversed(created):
                try:
                    obj.free()
                except Exception as exc:
                    errors.append(str(exc))
            tracked = getattr(self.agx, "tracked_objects", None)
            if tracked is not None:
                for key in list(tracked):
                    if key not in self._baseline_tracked_objects:
                        tracked.pop(key, None)
        if self._temporary_frame_path is not None:
            try:
                self._temporary_frame_path.unlink(missing_ok=True)
            except OSError as exc:
                errors.append(str(exc))
        self._frame_cleaned = not errors
        return self._frame_cleaned

    def submit_frame(self, queue_index: int, timeout_s: float) -> dict:
        if queue_index != QUEUE_INDEX:
            raise RenderBackendError("private render requires queue index 1")
        if (
            isinstance(timeout_s, bool)
            or not isinstance(timeout_s, (int, float))
            or float(timeout_s) != COMPLETION_DEADLINE_S
        ):
            raise RenderBackendError("private render requires a fixed 0.5 second deadline")
        if not self._context_configured or self.renderer is None:
            raise RenderBackendError("context 63 is not configured")
        if self._last_receipt is not None or self._frame_cleaned:
            raise RenderBackendError("private render is one-shot")

        renderer = self.renderer
        ta_before = self._queue_pointers(renderer.wq_ta)
        d3_before = self._queue_pointers(renderer.wq_3d)
        event_before = int(self.agx.event_mgr.event_count)
        ta_stamp_before = self._stamp(renderer.stamp_ta1)
        d3_stamp_before = self._stamp(renderer.stamp_3d1)
        output = self._fixture_object(self.fixture.output_gpu_va)
        self._output_before = hashlib.sha256(self._object_bytes(output)).hexdigest()
        self._immutable_before = self._immutable_sha256()

        renderer.submit(self.frame.cmdbuf)
        ta_submit = self._queue_pointers(renderer.wq_ta)
        d3_submit = self._queue_pointers(renderer.wq_3d)
        if ta_submit["producer"] - ta_before["producer"] != 2:
            raise RenderBackendError("TA producer did not advance by two commands")
        if d3_submit["producer"] - d3_before["producer"] != 2:
            raise RenderBackendError("3D producer did not advance by two commands")
        renderer.run()

        started = self.clock()
        complete = False
        while self.clock() - started < float(timeout_s):
            self.agx.asc.work()
            self.agx.poll_channels()
            ta_final = self._queue_pointers(renderer.wq_ta)
            d3_final = self._queue_pointers(renderer.wq_3d)
            ta_stamp_after = self._stamp(renderer.stamp_ta1)
            d3_stamp_after = self._stamp(renderer.stamp_3d1)
            complete = (
                bool(renderer.ev_ta.fired)
                and bool(renderer.ev_3d.fired)
                and ta_final["read"] == ta_submit["producer"]
                and ta_final["done"] == ta_submit["producer"]
                and d3_final["read"] == d3_submit["producer"]
                and d3_final["done"] == d3_submit["producer"]
                and ta_stamp_after == int(renderer.stamp_value_ta)
                and d3_stamp_after == int(renderer.stamp_value_3d)
            )
            if complete:
                break
        elapsed = self.clock() - started

        ta_final = self._queue_pointers(renderer.wq_ta)
        d3_final = self._queue_pointers(renderer.wq_3d)
        ta_stamp_after = self._stamp(renderer.stamp_ta1)
        d3_stamp_after = self._stamp(renderer.stamp_3d1)
        event_after = int(self.agx.event_mgr.event_count)
        event_delta = event_after - event_before
        spurious = [event_delta] if event_delta < 0 or event_delta > 2 else []
        self._output_after = hashlib.sha256(self._object_bytes(output)).hexdigest()
        self._immutable_after = self._immutable_sha256()
        lifecycle_snapshot = self.lifecycle.snapshot("render-complete")
        firmware_faults = self._firmware_faults(lifecycle_snapshot)
        mapping = self.mapping_evidence()
        live_snapshot = self._snapshot_payload(lifecycle_snapshot)
        cleanup_complete = self._cleanup_frame()

        receipt = {
            "context_id": CONTEXT_ID,
            "page_size": PAGE_SIZE,
            "queue_index": QUEUE_INDEX,
            "ta_command_count": 2,
            "d3_command_count": 2,
            "ta_producer_before": ta_before["producer"],
            "ta_producer_after": ta_submit["producer"],
            "ta_read_before": ta_before["read"],
            "ta_read_after": ta_final["read"],
            "ta_done_before": ta_before["done"],
            "ta_done_after": ta_final["done"],
            "d3_producer_before": d3_before["producer"],
            "d3_producer_after": d3_submit["producer"],
            "d3_read_before": d3_before["read"],
            "d3_read_after": d3_final["read"],
            "d3_done_before": d3_before["done"],
            "d3_done_after": d3_final["done"],
            "wrap_ambiguous": False,
            "ta_event_id": int(renderer.ev_ta.id),
            "d3_event_id": int(renderer.ev_3d.id),
            "event_ta_matches": int(bool(renderer.ev_ta.fired)),
            "event_3d_matches": int(bool(renderer.ev_3d.fired)),
            "spurious_events": spurious,
            "ta_stamp_before": ta_stamp_before,
            "ta_stamp_after": ta_stamp_after,
            "d3_stamp_before": d3_stamp_before,
            "d3_stamp_after": d3_stamp_after,
            "output_sha256_before": self._output_before,
            "output_sha256_after": self._output_after,
            "immutable_sha256_before": self._immutable_before,
            "immutable_sha256_after": self._immutable_after,
            "guards_unmapped": mapping["guards_unmapped"],
            "declared_mapping_count": mapping["declared_mapping_count"],
            "mapping_classification": mapping["mapping_classification"],
            "unexpected_mappings": mapping["unexpected_mappings"],
            "firmware_faults": firmware_faults,
            "physical_fault_readable": False,
            "physical_fault_value": None,
            "cleanup_complete": cleanup_complete,
            "elapsed_s": float(elapsed),
            "deadline_s": float(timeout_s),
        }
        self._last_receipt = dict(receipt)
        live_snapshot["temporary_frame"]["exists"] = False
        live_snapshot["completion"] = dict(receipt)
        self._last_snapshot = live_snapshot

        if spurious:
            raise RenderBackendError("spurious completion events observed")
        no_progress = (
            ta_final["read"] == ta_before["read"]
            and ta_final["done"] == ta_before["done"]
            and d3_final["read"] == d3_before["read"]
            and d3_final["done"] == d3_before["done"]
            and not renderer.ev_ta.fired
            and not renderer.ev_3d.fired
        )
        if not complete and elapsed >= float(timeout_s) and no_progress:
            raise RenderBackendError("render completion timeout")
        if not renderer.ev_ta.fired or not renderer.ev_3d.fired:
            missing = "TA" if not renderer.ev_ta.fired else "3D"
            if not renderer.ev_ta.fired and not renderer.ev_3d.fired:
                missing = "event"
            raise RenderBackendError(f"{missing} completion event missing")
        if ta_final["done"] != ta_submit["producer"] or d3_final["done"] != d3_submit["producer"]:
            raise RenderBackendError("queue done pointer did not reach producer")
        if ta_final["read"] != ta_submit["producer"]:
            raise RenderBackendError("TA read pointer did not reach producer")
        if d3_final["read"] != d3_submit["producer"]:
            raise RenderBackendError("3D read pointer did not reach producer")
        if not complete and elapsed >= float(timeout_s):
            raise RenderBackendError("render completion timeout")
        if self._output_after != self.fixture.expected_output_sha256:
            raise RenderBackendError("output hash does not match fixture oracle")
        try:
            return validate_render_completion(receipt, self.fixture)
        except RenderGateError as exc:
            raise RenderBackendError(str(exc)) from exc

    def _snapshot_payload(self, lifecycle_snapshot=None) -> dict:
        lifecycle_snapshot = lifecycle_snapshot or self.lifecycle.snapshot("render-snapshot")
        if self._frame_cleaned and self._last_snapshot is not None:
            payload = copy.deepcopy(self._last_snapshot)
            payload["firmware"] = lifecycle_snapshot.get("firmware")
            payload["fault"] = lifecycle_snapshot.get("fault")
            payload["sgx_irqs"] = lifecycle_snapshot.get("sgx_irqs", [])
            payload["temporary_frame"]["exists"] = bool(
                self._temporary_frame_path and self._temporary_frame_path.exists()
            )
            return payload
        renderer = self.renderer
        return {
            "firmware": lifecycle_snapshot.get("firmware"),
            "queues": None if renderer is None else {
                "ta": self._queue_pointers(renderer.wq_ta),
                "3d": self._queue_pointers(renderer.wq_3d),
            },
            "events": None if renderer is None else {
                "ta_id": int(renderer.ev_ta.id),
                "ta_fired": bool(renderer.ev_ta.fired),
                "3d_id": int(renderer.ev_3d.id),
                "3d_fired": bool(renderer.ev_3d.fired),
                "count": int(self.agx.event_mgr.event_count),
            },
            "stamps": None if renderer is None else {
                "ta": self._stamp(renderer.stamp_ta1),
                "3d": self._stamp(renderer.stamp_3d1),
            },
            "mapping": self.mapping_evidence() if self._mapping_evidence else None,
            "output": {"before": self._output_before, "after": self._output_after},
            "immutable": {
                "before": self._immutable_before, "after": self._immutable_after
            },
            "fault": lifecycle_snapshot.get("fault"),
            "sgx_irqs": lifecycle_snapshot.get("sgx_irqs", []),
            "temporary_frame": {
                "path": str(self._temporary_frame_path) if self._temporary_frame_path else None,
                "exists": bool(
                    self._temporary_frame_path and self._temporary_frame_path.exists()
                ),
            },
            "completion": dict(self._last_receipt) if self._last_receipt else None,
        }

    def snapshot(self, reason: str) -> dict:
        payload = self._snapshot_payload(self.lifecycle.snapshot(reason))
        payload["reason"] = reason
        self._last_snapshot = payload
        return payload

    def stop(self) -> None:
        self.lifecycle.stop()

    def reset(self) -> None:
        self._cleanup_frame()
        if self._context_configured and self.agx is not None:
            uat = self.agx.uat
            with uat.handoff.lock():
                uat.set_l0(CONTEXT_ID, 0, 0, CONTEXT_ID)
                uat.set_l0(CONTEXT_ID, 1, 0, CONTEXT_ID)
                uat.flush_dirty()
                uat.invalidate_cache()
            self._roots_cleared = True
            if self.renderer is not None and not self._events_freed:
                self.agx.event_mgr.free_event(self.renderer.ev_ta)
                self.agx.event_mgr.free_event(self.renderer.ev_3d)
                self._events_freed = True
        self._context_configured = False
        self.context = None
        self.frame = None
        self.renderer = None
        self.lifecycle.reset()
        self.agx = None

    def released(self) -> bool:
        temp_absent = (
            self._temporary_frame_path is None
            or not self._temporary_frame_path.exists()
        )
        return (
            self.lifecycle.released()
            and not self._context_configured
            and self.context is None
            and self.frame is None
            and self.renderer is None
            and not self._configuration_failed
            and temp_absent
        )
