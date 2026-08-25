import unittest
from pathlib import Path
import subprocess
from types import SimpleNamespace

from tools.agx_contract import load_contract
from tools.agx_queue_gate import CONTEXT_ID, PAGE_SIZE


CONTRACT = load_contract("config/j313-agx.json")


class FakeInterface:
    def __init__(self):
        self.memory = {}

    def writemem(self, address, data):
        self.memory[address] = bytes(data)

    def readmem(self, address, size):
        return self.memory.get(address, bytes(size))[:size]


class FakeProxy:
    def __init__(self):
        self.memsets = []

    def memset32(self, address, value, size):
        self.memsets.append((address, value, size))


class FakeU:
    def __init__(self):
        self.iface = FakeInterface()
        self.proxy = FakeProxy()
        self._next = 0x900000000
        self.allocations = []

    def memalign(self, alignment, size):
        address = (self._next + alignment - 1) & ~(alignment - 1)
        self._next = address + size
        self.allocations.append((alignment, size, address))
        return address


class FakeHandoff:
    def __init__(self, calls):
        self.calls = calls

    class Lock:
        def __init__(self, calls):
            self.calls = calls

        def __enter__(self):
            self.calls.append("lock-enter")
            return self

        def __exit__(self, exc_type, exc, tb):
            self.calls.append("lock-exit")
            return False

    def lock(self):
        return self.Lock(self.calls)


class FakeUAT:
    def __init__(self, *, page_size=PAGE_SIZE):
        self.PAGE_SIZE = page_size
        self.ttbr1_base = 0x880000000
        self.calls = []
        self.handoff = FakeHandoff(self.calls)
        self.binds = []
        self.maps = []

    def bind_context(self, context_id, ttbr0):
        self.binds.append((context_id, ttbr0))

    def iomap_at(self, context_id, va, pa, size, **flags):
        self.maps.append((context_id, va, pa, size, flags))

    def iotranslate(self, context_id, va, size):
        for mapped_context, mapped_va, pa, mapped_size, _ in self.maps:
            if mapped_context == context_id and mapped_va == va and mapped_size == size:
                return [(pa, size)]
        return [(None, size)]

    def flush_dirty(self):
        self.calls.append("flush")

    def invalidate_cache(self):
        self.calls.append("invalidate")

    def set_l0(self, context_id, root, base, asid=0):
        self.calls.append(("set_l0", context_id, root, base, asid))


class FakeObject:
    _next_address = 0x700000000

    def __init__(self, kind):
        self.kind = kind
        self._addr = FakeObject._next_address
        self._paddr = self._addr + 0x100000
        FakeObject._next_address += 0x4000
        self.push_count = 0
        if kind == "stamp":
            self.value = 0

    def push(self):
        self.push_count += 1
        return self


class FakeAllocator:
    def __init__(self):
        self.objects = []

    def new(self, object_type, **kwargs):
        names = {
            FakeGPUContextData: "scheduler",
            FakeJobList: "job-list",
            FakeStampCounter: "stamp",
            FakeWorkCommandBarrier: "barrier",
        }
        obj = FakeObject(names[object_type])
        self.objects.append(obj)
        return obj


class FakeGPUContextData:
    pass


class FakeJobList:
    pass


class FakeStampCounter:
    pass


class FakeWorkCommandBarrier:
    pass


class FakeRegister:
    def __init__(self, value=0):
        self.val = value


class FakePointerMap:
    def __init__(self):
        self.GPU_DONEPTR = FakeRegister(0)


class FakeGPU3DWorkQueue:
    TYPE = 1

    def __init__(self, agx, scheduler_context, job_list):
        self.agx = agx
        self.scheduler_context = scheduler_context
        self.job_list = job_list
        self.wptr = agx.initial_wptr
        self.rb_size = agx.rb_size
        self.pmap = FakePointerMap()
        self.info = FakeObject("queue-info")
        self.submitted = []

    def submit(self, work):
        work.push()
        self.submitted.append(work)
        self.wptr = (self.wptr + 1) % self.rb_size


class FakeEvent:
    def __init__(self, event_id):
        self.id = event_id
        self.fired = False


class FakeEventManager:
    def __init__(self):
        self.event_count = 7
        self.event = None
        self.freed = []

    def allocate_event(self):
        self.event = FakeEvent(0)
        return self.event

    def free_event(self, event):
        self.freed.append(event)


class FakeQueueChannel:
    def __init__(self):
        self.runs = []

    def run(self, queue, event_id):
        self.runs.append((queue, event_id))


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def advance(self, amount=0.1):
        self.now += amount


QUEUE_TYPES = SimpleNamespace(
    GPUContextData=FakeGPUContextData,
    JobList=FakeJobList,
    GPU3DWorkQueue=FakeGPU3DWorkQueue,
    StampCounter=FakeStampCounter,
    WorkCommandBarrier=FakeWorkCommandBarrier,
)


class FakeAgx:
    PAGE_SIZE = PAGE_SIZE

    def __init__(self, u, *, page_size=PAGE_SIZE):
        self.u = u
        self.p = u.proxy
        self.iface = u.iface
        self.uat = FakeUAT(page_size=page_size)
        self.kobj = FakeAllocator()
        self.kshared = FakeAllocator()
        self.event_mgr = FakeEventManager()
        self.initial_wptr = 0
        self.rb_size = 0x100
        self.clock = FakeClock()
        self.asc = SimpleNamespace(work=self._work)
        self.poll_calls = 0
        self.poll_behavior = None
        self.ch = SimpleNamespace(
            queue=[
                SimpleNamespace(q_3D=FakeQueueChannel(), q_TA=FakeQueueChannel()),
                SimpleNamespace(q_3D=FakeQueueChannel(), q_TA=FakeQueueChannel()),
            ]
        )

    def _work(self):
        self.clock.advance()

    def poll_channels(self):
        self.poll_calls += 1
        if self.poll_behavior:
            self.poll_behavior(self)


class FakeLifecycle:
    def __init__(self, agx):
        self.agx = agx
        self.calls = []

    def prepare(self, contract):
        self.calls.append("prepare")

    def start(self):
        self.calls.append("start")

    def heartbeat(self):
        self.calls.append("heartbeat")
        return {"progress": True}

    def snapshot(self, reason):
        self.calls.append(("snapshot", reason))
        return {
            "reason": reason,
            "firmware": {"asc_running": True},
            "fault": {"source": "firmware-shared-memory", "faulted": False},
            "sgx_irqs": [0, 0, 0],
        }

    def stop(self):
        self.calls.append("stop")

    def reset(self):
        self.calls.append("reset")
        self.agx.uat.calls.append("lifecycle-reset")

    def released(self):
        self.calls.append("released")
        return True


class ContextIsolationTests(unittest.TestCase):
    def _backend(self, *, page_size=PAGE_SIZE):
        from tools.agx_m1n1_queue_backend import M1n1AgxQueueBackend

        u = FakeU()
        agx = FakeAgx(u, page_size=page_size)
        lifecycle = FakeLifecycle(agx)
        backend = M1n1AgxQueueBackend(
            u,
            lifecycle=lifecycle,
            queue_types=QUEUE_TYPES,
            clock=agx.clock,
        )
        backend.prepare(CONTRACT)
        backend.start()
        return backend, u, agx, lifecycle

    def test_context_63_has_one_canary_mapping_and_unmapped_guards(self):
        from tools.agx_m1n1_queue_backend import (
            CANARY_SEED,
            GATE_VA,
            GUARD_AFTER_VA,
            GUARD_BEFORE_VA,
        )

        backend, u, agx, _ = self._backend()
        backend.configure_context(CONTEXT_ID)

        self.assertEqual(len(u.allocations), 2)
        ttbr = u.allocations[0][2]
        canary_pa = u.allocations[1][2]
        self.assertEqual(u.allocations[0][:2], (PAGE_SIZE, PAGE_SIZE))
        self.assertEqual(u.allocations[1][:2], (PAGE_SIZE, PAGE_SIZE))
        self.assertEqual(u.proxy.memsets, [(ttbr, 0, PAGE_SIZE)])
        self.assertEqual(agx.uat.binds, [(CONTEXT_ID, ttbr)])
        self.assertEqual(
            agx.uat.maps,
            [(CONTEXT_ID, GATE_VA, canary_pa, PAGE_SIZE, {})],
        )
        expected = (CANARY_SEED * ((PAGE_SIZE + len(CANARY_SEED) - 1) // len(CANARY_SEED)))[:PAGE_SIZE]
        self.assertEqual(u.iface.memory[canary_pa], expected)
        evidence = backend.mapping_evidence()
        self.assertEqual(evidence["declared_mapping_count"], 1)
        self.assertEqual(evidence["unexpected_mappings"], [])
        self.assertTrue(evidence["guards_unmapped"])
        self.assertEqual(evidence["guard_before_va"], GUARD_BEFORE_VA)
        self.assertEqual(evidence["guard_after_va"], GUARD_AFTER_VA)

    def test_every_context_other_than_63_is_rejected(self):
        from tools.agx_m1n1_queue_backend import QueueBackendError

        backend, _, _, _ = self._backend()
        for context_id in (0, 1, 62, 64):
            with self.subTest(context_id=context_id):
                with self.assertRaisesRegex(QueueBackendError, "context 63"):
                    backend.configure_context(context_id)

    def test_non_16k_uat_page_is_rejected_without_allocation(self):
        from tools.agx_m1n1_queue_backend import QueueBackendError

        backend, u, agx, _ = self._backend(page_size=0x1000)
        with self.assertRaisesRegex(QueueBackendError, "0x4000"):
            backend.configure_context(CONTEXT_ID)
        self.assertEqual(u.allocations, [])
        self.assertEqual(agx.uat.binds, [])
        self.assertEqual(agx.uat.maps, [])

    def test_context_cannot_be_configured_twice(self):
        from tools.agx_m1n1_queue_backend import QueueBackendError

        backend, _, _, _ = self._backend()
        backend.configure_context(CONTEXT_ID)
        with self.assertRaisesRegex(QueueBackendError, "already configured"):
            backend.configure_context(CONTEXT_ID)


class QueueSubmissionTests(ContextIsolationTests):
    def _configured_backend(self):
        backend, _, agx, lifecycle = self._backend()
        backend.configure_context(CONTEXT_ID)
        return backend, agx, lifecycle

    @staticmethod
    def _complete_on_first_poll(agx):
        agx.poll_behavior = None
        agx._test_queue.pmap.GPU_DONEPTR.val = 1
        agx.event_mgr.event.fired = True
        agx.event_mgr.event_count += 1

    def test_one_barrier_uses_only_queue_one_3d_channel(self):
        from tools.agx_m1n1_queue_backend import STAMP_VALUE

        backend, agx, _ = self._configured_backend()

        def complete(target):
            target._test_queue = backend._queue
            self._complete_on_first_poll(target)

        agx.poll_behavior = complete
        receipt = backend.submit_barrier(1, 0.5)

        queue = backend._queue
        barrier = backend._barrier
        self.assertEqual(len(queue.submitted), 1)
        self.assertIs(queue.submitted[0], barrier)
        self.assertEqual(barrier.push_count, 1)
        self.assertIs(barrier.stamp, backend._stamp)
        self.assertEqual(barrier.wait_value, STAMP_VALUE)
        self.assertEqual(barrier.stamp_self, STAMP_VALUE)
        self.assertEqual(barrier.event, 0)
        self.assertEqual(len(agx.ch.queue[1].q_3D.runs), 1)
        self.assertEqual(agx.ch.queue[0].q_3D.runs, [])
        self.assertEqual(agx.ch.queue[1].q_TA.runs, [])
        self.assertEqual(receipt["producer_after"], 1)
        self.assertEqual(receipt["consumer_after"], 1)
        self.assertEqual(receipt["event_count_after"], 8)
        self.assertEqual(receipt["matching_event_count"], 1)
        self.assertEqual(agx.poll_calls, 1)

    def test_timeout_records_final_pointers_and_fails(self):
        from tools.agx_m1n1_queue_backend import QueueBackendError

        backend, agx, _ = self._configured_backend()
        with self.assertRaisesRegex(QueueBackendError, "timeout"):
            backend.submit_barrier(1, 0.5)
        self.assertGreaterEqual(agx.poll_calls, 5)
        self.assertEqual(backend._queue.pmap.GPU_DONEPTR.val, 0)

    def test_event_without_done_pointer_is_rejected(self):
        from tools.agx_m1n1_queue_backend import QueueBackendError

        backend, agx, _ = self._configured_backend()

        def event_only(target):
            target.poll_behavior = None
            target.event_mgr.event.fired = True
            target.event_mgr.event_count += 1

        agx.poll_behavior = event_only
        with self.assertRaisesRegex(QueueBackendError, "consumer"):
            backend.submit_barrier(1, 0.5)

    def test_done_pointer_without_event_times_out(self):
        from tools.agx_m1n1_queue_backend import QueueBackendError

        backend, agx, _ = self._configured_backend()

        def done_only(target):
            target._test_queue = backend._queue
            target._test_queue.pmap.GPU_DONEPTR.val = 1
            target.poll_behavior = None

        agx.poll_behavior = done_only
        with self.assertRaisesRegex(QueueBackendError, "timeout"):
            backend.submit_barrier(1, 0.5)

    def test_duplicate_event_is_rejected(self):
        from tools.agx_m1n1_queue_backend import QueueBackendError

        backend, agx, _ = self._configured_backend()

        def duplicate(target):
            target._test_queue = backend._queue
            target._test_queue.pmap.GPU_DONEPTR.val = 1
            target.event_mgr.event.fired = True
            target.event_mgr.event_count += 2
            target.poll_behavior = None

        agx.poll_behavior = duplicate
        with self.assertRaisesRegex(QueueBackendError, "event_count|matching_event_count"):
            backend.submit_barrier(1, 0.5)

    def test_spurious_event_id_times_out(self):
        from tools.agx_m1n1_queue_backend import QueueBackendError

        backend, agx, _ = self._configured_backend()

        def spurious(target):
            target.event_mgr.event_count += 1
            target.poll_behavior = None

        agx.poll_behavior = spurious
        with self.assertRaisesRegex(QueueBackendError, "timeout"):
            backend.submit_barrier(1, 0.5)

    def test_ring_wrap_is_rejected_before_channel_run(self):
        from tools.agx_m1n1_queue_backend import QueueBackendError

        backend, agx, _ = self._configured_backend()
        agx.initial_wptr = agx.rb_size - 1
        with self.assertRaisesRegex(QueueBackendError, "wrap"):
            backend.submit_barrier(1, 0.5)
        self.assertEqual(agx.ch.queue[1].q_3D.runs, [])

    def test_queue_index_and_deadline_are_fixed(self):
        from tools.agx_m1n1_queue_backend import QueueBackendError

        backend, _, _ = self._configured_backend()
        with self.assertRaisesRegex(QueueBackendError, "queue index 1"):
            backend.submit_barrier(0, 0.5)
        with self.assertRaisesRegex(QueueBackendError, "0.5"):
            backend.submit_barrier(1, 1.0)


class EvidenceAndTeardownTests(QueueSubmissionTests):
    def _completed_backend(self):
        backend, agx, lifecycle = self._configured_backend()

        def complete(target):
            backend._queue.pmap.GPU_DONEPTR.val = 1
            target.event_mgr.event.fired = True
            target.event_mgr.event_count += 1
            target.poll_behavior = None

        agx.poll_behavior = complete
        backend.submit_barrier(1, 0.5)
        return backend, agx, lifecycle

    def test_snapshot_contains_every_queue_and_mapping_evidence_class(self):
        backend, _, _ = self._completed_backend()
        snapshot = backend.snapshot("qualified")

        self.assertEqual(snapshot["reason"], "qualified")
        self.assertIn("firmware", snapshot)
        self.assertIn("fault", snapshot)
        self.assertIn("sgx_irqs", snapshot)
        self.assertEqual(snapshot["queue"]["producer"], 1)
        self.assertEqual(snapshot["queue"]["consumer"], 1)
        self.assertEqual(snapshot["queue"]["event_id"], 0)
        self.assertEqual(snapshot["queue"]["event_count"], 8)
        self.assertTrue(snapshot["queue"]["event_fired"])
        self.assertEqual(snapshot["queue"]["stamp"], 0x51000000)
        self.assertEqual(snapshot["mapping"]["declared_mapping_count"], 1)
        self.assertTrue(snapshot["mapping"]["guards_unmapped"])
        self.assertEqual(
            snapshot["canary"]["sha256_before"],
            snapshot["canary"]["sha256_live"],
        )
        self.assertFalse(snapshot["canary"]["changed"])
        self.assertEqual(
            snapshot["physical_fault"],
            {"readable": False, "reason": "power-domain-not-qualified"},
        )

    def test_snapshot_exposes_live_canary_mutation(self):
        backend, agx, _ = self._completed_backend()
        agx.iface.writemem(backend._canary_pa, b"X" * PAGE_SIZE)
        snapshot = backend.snapshot("mutated")
        self.assertTrue(snapshot["canary"]["changed"])
        self.assertNotEqual(
            snapshot["canary"]["sha256_before"],
            snapshot["canary"]["sha256_live"],
        )

    def test_physical_fault_register_is_never_read(self):
        backend, agx, _ = self._completed_backend()

        class ForbiddenFaultRegister:
            @property
            def val(self):
                raise AssertionError("power-gated fault register was read")

        agx.physical_fault = ForbiddenFaultRegister()
        snapshot = backend.snapshot("safe")
        self.assertFalse(snapshot["physical_fault"]["readable"])

    def test_reset_clears_context_63_before_g1_context_zero_reset(self):
        backend, agx, lifecycle = self._completed_backend()
        agx.uat.calls.clear()

        backend.stop()
        backend.reset()

        self.assertEqual(
            agx.uat.calls,
            [
                "lock-enter",
                ("set_l0", 63, 0, 0, 63),
                ("set_l0", 63, 1, 0, 63),
                "flush",
                "invalidate",
                "lock-exit",
                "lifecycle-reset",
            ],
        )
        self.assertEqual(agx.event_mgr.freed, [agx.event_mgr.event])
        self.assertTrue(backend.released())
        self.assertEqual(lifecycle.calls[-1], "released")


class QueueSourceBoundaryTests(unittest.TestCase):
    def test_default_type_loader_uses_bundled_proxyclient(self):
        root = Path(__file__).resolve().parents[1]
        result = subprocess.run(
            [
                str(root / "proxyenv" / "bin" / "python"),
                "-c",
                (
                    "from tools.agx_m1n1_queue_backend import "
                    "_default_queue_types; "
                    "print(_default_queue_types().GPU3DWorkQueue.__name__)"
                ),
            ],
            cwd=root,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "GPU3DWorkQueue")

    def test_adapter_has_no_render_display_or_guest_memory_path(self):
        source = (
            Path(__file__).resolve().parents[1]
            / "tools"
            / "agx_m1n1_queue_backend.py"
        ).read_text()
        for forbidden in (
            "m1n1.agx.render",
            "GPURenderer",
            "GPUFrame",
            "WorkCommand3D",
            "WorkCommandTA",
            "shader",
            "framebuffer",
            "hv_ipa_to_pa",
            "guest_memory",
            "GPUContext(",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, source)
        self.assertIn("ch.queue[QUEUE_INDEX].q_3D.run", source)
        self.assertNotRegex(source, r"ch\.queue\[[0-9]+\]")


if __name__ == "__main__":
    unittest.main()
