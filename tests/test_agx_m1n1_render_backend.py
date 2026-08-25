import hashlib
import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
import zipfile

from tools.agx_contract import load_contract
from tools.agx_frame_fixture import FrameObject, ValidatedFrame


ROOT = Path(__file__).resolve().parents[1]
CONTRACT = ROOT / "config" / "j313-agx.json"
EXPECTED_BYTES = bytes([0x11, 0x22, 0x33, 0xFF]) * 0x1000
POISON_BYTES = bytes([0xA5]) * 0x4000
PIPELINE_BYTES = bytes([0x5A]) * 0x4000
OUTPUT_HASH = hashlib.sha256(EXPECTED_BYTES).hexdigest()
POISON_HASH = hashlib.sha256(POISON_BYTES).hexdigest()


def fixture():
    objects = (
        FrameObject(
            name="pipeline",
            gpu_va=0x1100010000,
            size=0x4000,
            map_flags=(("Access", 3),),
            sha256=hashlib.sha256(PIPELINE_BYTES).hexdigest(),
            data=PIPELINE_BYTES,
        ),
        FrameObject(
            name="output",
            gpu_va=0x1500000000,
            size=0x4000,
            map_flags=(("Access", 3),),
            sha256=POISON_HASH,
            data=POISON_BYTES,
        ),
    )
    return ValidatedFrame(
        fixture_sha256="a" * 64,
        command_buffer={"encoder": 0x1100010000, "output": 0x1500000000},
        objects=objects,
        output_gpu_va=0x1500000000,
        output_size=0x4000,
        poison_sha256=POISON_HASH,
        expected_output_sha256=OUTPUT_HASH,
    )


class Value:
    def __init__(self, value=0): self.val = value


class FakeObject:
    def __init__(self, agx, context, address, size, name, data=b""):
        self.agx = agx
        self._ctx = context
        self._addr = address
        self._addr_align = address
        self._size = size
        self._size_align = size
        self._name = name
        self.val = bytes(data).ljust(size, b"\0")
        self._dead = False
        agx.all_objects[(context, address)] = self
        agx.tracked_objects[(context, address)] = self

    def pull(self): return self
    def push(self): return self
    def free(self):
        if not self._dead:
            self._dead = True
            self.agx.all_objects.pop((self._ctx, self._addr), None)


class FakeUAT:
    PAGE_SIZE = 0x4000
    def __init__(self): self.calls = []; self.handoff = self
    def bind_context(self, context, root): self.calls.append(("bind", context, root))
    def lock(self): return self
    def __enter__(self): self.calls.append("lock"); return self
    def __exit__(self, *args): self.calls.append("unlock")
    def set_l0(self, *args): self.calls.append(("set_l0",) + args)
    def flush_dirty(self): self.calls.append("flush")
    def invalidate_cache(self): self.calls.append("invalidate")


class FakeEvent:
    def __init__(self, event_id): self.id = event_id; self.fired = False


class FakeEventManager:
    def __init__(self): self.event_count = 0; self.freed = []
    def free_event(self, event): self.freed.append(event.id)


class FakeChannel:
    def __init__(self, name, order): self.name = name; self.order = order
    def run(self, queue, event): self.order.append((self.name, event))


class FakeQueue:
    def __init__(self):
        self.wptr = 0
        self.pmap = SimpleNamespace(
            GPU_RPTR=Value(0), GPU_DONEPTR=Value(0), CPU_WPTR=Value(0)
        )


class FakeStamp:
    def __init__(self, value): self.value = value; self.dead = False
    def pull(self):
        if self.dead: raise RuntimeError("stamp was freed")
        return self


class FakeWork:
    def __init__(self, renderer): self.renderer = renderer; self.freed = False
    def free(self):
        self.freed = True
        for stamp in (
            self.renderer.stamp_ta1, self.renderer.stamp_ta2,
            self.renderer.stamp_3d1, self.renderer.stamp_3d2,
        ): stamp.dead = True


class FakeContext:
    def __init__(self, agx):
        self.agx = agx; self.ctx = None; self.objects = {}
        self.ttbr0_base = 0x900000000
    def bind(self, context):
        self.ctx = context
        self.agx.uat.bind_context(context, self.ttbr0_base)
        thing = FakeObject(self.agx, context, 0x6FFFFF8000, 0x4000, "thing")
        self.objects[thing._addr] = thing
    def free(self, obj): obj.free(); self.objects.pop(obj._addr, None)


class FakeFrame:
    def __init__(self, context, filename, track=False):
        self.ctx = context; self.objects = []
        with zipfile.ZipFile(filename) as archive:
            self.cmdbuf = json.loads(archive.read("cmdbuf.json"))
            for item in json.loads(archive.read("objects.json")):
                data = archive.read(item["file"])
                obj = FakeObject(
                    context.agx, context.ctx, item["addr"], item["size"],
                    item["name"], data,
                )
                context.objects[obj._addr] = obj
                self.objects.append(obj)


class FakeRenderer:
    def __init__(self, context, buffers=16, bm_slot=0, queue=1):
        self.ctx = context; self.agx = context.agx; self.queue = queue
        self.wq_ta = FakeQueue(); self.wq_3d = FakeQueue()
        self.stamp_value_ta = 0x7A000000
        self.stamp_value_3d = 0x3D000000
        self.stamp_ta1 = FakeStamp(self.stamp_value_ta)
        self.stamp_ta2 = FakeStamp(self.stamp_value_ta)
        self.stamp_3d1 = FakeStamp(self.stamp_value_3d)
        self.stamp_3d2 = FakeStamp(self.stamp_value_3d)
        self.ev_ta = FakeEvent(7); self.ev_3d = FakeEvent(8)
        self.work = []; self.submit_calls = []
        FakeObject(self.agx, 63, 0x1600010000, 0x4000, "renderer-private")
        FakeObject(self.agx, 0, 0x100004000, 0x4000, "renderer-firmware")
        self.agx.renderer = self
    def submit(self, cmdbuf):
        self.submit_calls.append(cmdbuf)
        self.wq_ta.wptr += 2; self.wq_3d.wptr += 2
        self.work.append(FakeWork(self))
        self.stamp_value_ta += 0x100; self.stamp_value_3d += 0x100
        return self.work[-1]
    def run(self):
        channels = self.agx.ch.queue[self.queue]
        channels.q_3D.run(self.wq_3d, self.ev_3d.id)
        channels.q_TA.run(self.wq_ta, self.ev_ta.id)


class FailingRenderer:
    def __init__(self, context, buffers=16, bm_slot=0, queue=1):
        FakeObject(context.agx, 63, 0x1600010000, 0x4000, "partial-renderer")
        raise RuntimeError("renderer construction failed")


class FakeASC:
    def __init__(self, agx): self.agx = agx
    def work(self): self.agx.advance()


class FakeAGX:
    PAGE_SIZE = 0x4000
    def __init__(self, mode="complete"):
        self.mode = mode; self.uat = FakeUAT(); self.all_objects = {}
        self.tracked_objects = {}
        self.event_mgr = FakeEventManager(); self.order = []; self.renderer = None
        self.ch = SimpleNamespace(queue=[None, SimpleNamespace(
            q_3D=FakeChannel("3D", self.order), q_TA=FakeChannel("TA", self.order)
        )])
        self.asc = FakeASC(self); self.polls = 0
        FakeObject(self, 0, 0x100000000, 0x4000, "firmware-baseline")
    def poll_channels(self): self.polls += 1
    def advance(self):
        renderer = self.renderer
        if renderer is None: return
        mode = self.mode
        if mode == "timeout": return
        if mode not in ("event-only", "ta-only"):
            renderer.wq_3d.pmap.GPU_RPTR.val = 2
            renderer.wq_3d.pmap.GPU_DONEPTR.val = 2
            renderer.stamp_3d1.value = renderer.stamp_value_3d
            renderer.stamp_3d2.value = renderer.stamp_value_3d
        if mode not in ("event-only", "d3-only"):
            renderer.wq_ta.pmap.GPU_RPTR.val = 2
            renderer.wq_ta.pmap.GPU_DONEPTR.val = 2
            renderer.stamp_ta1.value = renderer.stamp_value_ta
            renderer.stamp_ta2.value = renderer.stamp_value_ta
        if mode not in ("done-only", "ta-only"):
            renderer.ev_3d.fired = True
        if mode not in ("done-only", "d3-only"):
            renderer.ev_ta.fired = True
        if renderer.ev_3d.fired and renderer.ev_ta.fired:
            self.event_mgr.event_count = 2 if mode != "spurious" else 3
        if mode == "complete":
            output = self.all_objects[(63, 0x1500000000)]
            output.val = EXPECTED_BYTES


class FakeLifecycle:
    def __init__(self, agx): self.agx = agx; self.calls = []; self.open = False
    def prepare(self, contract): self.calls.append("prepare")
    def start(self): self.calls.append("start"); self.open = True
    def heartbeat(self): return {"alive": True}
    def snapshot(self, reason): return {
        "firmware": {"m1n1_base": 0x804000000, "proxy_identity": "proxy-a"},
        "fault": {"queue_uuid": 0, "unk_0": 0}, "sgx_irqs": [1, 2],
    }
    def stop(self): self.calls.append("stop")
    def reset(self): self.calls.append("reset"); self.open = False
    def released(self): return not self.open


class Clock:
    def __init__(self): self.value = 0.0
    def __call__(self): self.value += 0.01; return self.value


def types():
    return SimpleNamespace(GPUContext=FakeContext, GPUFrame=FakeFrame, GPURenderer=FakeRenderer)


class RenderBackendTests(unittest.TestCase):
    def setUp(self):
        self.agx = FakeAGX()
        self.lifecycle = FakeLifecycle(self.agx)
        self.backend = self._backend()
        self.contract = load_contract(CONTRACT)
        self.backend.prepare(self.contract, fixture())
        self.backend.start()

    def _backend(self, agx=None):
        from tools.agx_m1n1_render_backend import M1n1AgxRenderBackend
        agx = agx or self.agx
        lifecycle = self.lifecycle if agx is self.agx else FakeLifecycle(agx)
        return M1n1AgxRenderBackend(
            SimpleNamespace(base=0x804000000), lifecycle=lifecycle,
            render_types=types(), clock=Clock(),
        )

    def test_context_63_maps_fixture_and_classifies_every_object(self):
        self.backend.configure_context(63)
        mapping = self.backend.mapping_evidence()
        self.assertEqual(mapping["declared_mapping_count"], len(mapping["mapping_classification"]))
        self.assertEqual(
            {entry["class"] for entry in mapping["mapping_classification"]},
            {"bootstrap", "frame", "renderer", "firmware-shared"},
        )
        self.assertEqual(mapping["unexpected_mappings"], [])
        self.assertTrue(mapping["guards_unmapped"])
        self.assertTrue(self.backend.temporary_frame_path.exists())

    def test_wrong_context_or_page_size_fails_before_replay(self):
        from tools.agx_m1n1_render_backend import RenderBackendError
        with self.assertRaisesRegex(RenderBackendError, "context 63"):
            self.backend.configure_context(0)
        agx = FakeAGX(); agx.uat.PAGE_SIZE = 0x1000
        backend = self._backend(agx); backend.prepare(self.contract, fixture()); backend.start()
        with self.assertRaisesRegex(RenderBackendError, "page size"):
            backend.configure_context(63)

    def test_unexpected_mapping_is_rejected_before_submit(self):
        from tools.agx_m1n1_render_backend import RenderBackendError
        FakeObject(self.agx, 5, 0x1700000000, 0x4000, "foreign-context")
        with self.assertRaisesRegex(RenderBackendError, "unexpected mappings"):
            self.backend.configure_context(63)
        self.assertEqual(self.agx.order, [])

    def test_complete_frame_runs_3d_then_ta_and_returns_strict_receipt(self):
        from tools.agx_render_gate import validate_render_completion
        self.backend.configure_context(63)
        receipt = self.backend.submit_frame(1, 0.5)
        self.assertEqual(self.agx.order, [("3D", 8), ("TA", 7)])
        self.assertEqual(len(self.backend.renderer.submit_calls), 1)
        self.assertEqual(validate_render_completion(receipt, fixture()), receipt)
        self.assertEqual(receipt["output_sha256_before"], POISON_HASH)
        self.assertEqual(receipt["output_sha256_after"], OUTPUT_HASH)

    def test_event_pointer_stamp_output_and_timeout_failures_are_rejected(self):
        from tools.agx_m1n1_render_backend import RenderBackendError
        for mode, boundary in (
            ("event-only", "done"), ("done-only", "event"),
            ("ta-only", "3D"), ("d3-only", "TA"),
            ("spurious", "spurious"), ("wrong-output", "output"),
            ("timeout", "timeout"),
        ):
            with self.subTest(mode=mode):
                agx = FakeAGX(mode)
                backend = self._backend(agx)
                backend.prepare(self.contract, fixture()); backend.start()
                backend.configure_context(63)
                with self.assertRaisesRegex(RenderBackendError, boundary):
                    backend.submit_frame(1, 0.5)

    def test_snapshot_and_reset_preserve_evidence_then_release_everything(self):
        self.backend.configure_context(63)
        self.backend.submit_frame(1, 0.5)
        snapshot = self.backend.snapshot("test")
        for key in ("queues", "events", "stamps", "mapping", "output", "immutable", "fault", "sgx_irqs", "temporary_frame"):
            self.assertIn(key, snapshot)
        path = self.backend.temporary_frame_path
        self.backend.stop(); self.backend.reset()
        self.assertFalse(path.exists())
        self.assertIn(("set_l0", 63, 0, 0, 63), self.agx.uat.calls)
        self.assertIn(("set_l0", 63, 1, 0, 63), self.agx.uat.calls)
        self.assertIsNone(self.backend.renderer)
        self.assertEqual(
            set(self.agx.tracked_objects), {(0, 0x100000000)}
        )
        self.assertTrue(self.backend.released())

    def test_partial_renderer_construction_clears_roots_but_requires_cold_reset(self):
        from tools.agx_m1n1_render_backend import (
            M1n1AgxRenderBackend, RenderBackendError,
        )
        render_types = types()
        render_types.GPURenderer = FailingRenderer
        backend = M1n1AgxRenderBackend(
            SimpleNamespace(base=0x804000000), lifecycle=self.lifecycle,
            render_types=render_types, clock=Clock(),
        )
        backend.prepare(self.contract, fixture()); backend.start()
        with self.assertRaisesRegex(RuntimeError, "construction failed"):
            backend.configure_context(63)
        backend.stop(); backend.reset()
        self.assertIn(("set_l0", 63, 0, 0, 63), self.agx.uat.calls)
        self.assertFalse(backend.released())

    def test_real_source_boundary_matches_adapter_expectations(self):
        import sys
        proxyclient = ROOT / "m1n1_windows" / "proxyclient"
        if str(proxyclient) not in sys.path: sys.path.insert(0, str(proxyclient))
        from m1n1.agx.context import GPUContext
        from m1n1.agx.render import GPUFrame, GPURenderer
        for cls, names in (
            (GPUContext, ("bind", "free")),
            (GPUFrame, ("load",)),
            (GPURenderer, ("submit", "run")),
        ):
            for name in names: self.assertTrue(hasattr(cls, name))


if __name__ == "__main__": unittest.main()
