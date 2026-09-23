"""Offline executable specification of broker-v5 transactions; no m1n1 code."""
import unittest


class BrokerSpec:
    def __init__(self):
        self.firmware = (0x90000000, (0x12340003, 0x56780003))
        self.slots = {i: None for i in range(1, 64)}
        self.generation = 0
        self.processes = {}

    def create(self, identity, root, owned_pages):
        assert identity not in self.processes and root in owned_pages
        self.generation += 1
        self.processes[identity] = {
            "generation": self.generation, "root": root,
            "pages": set(owned_pages), "ptes": {}, "map_generation": 0,
        }

    def update(self, identity, va, pa, *, fail_before_ack=False):
        p = self.processes[identity]
        if va % 0x4000 or pa % 0x4000 or pa not in p["pages"]:
            raise ValueError("unowned or unaligned backing")
        old = p["ptes"].get(va)
        p["ptes"][va] = pa
        if fail_before_ack:
            if old is None:
                del p["ptes"][va]
            else:
                p["ptes"][va] = old
            raise RuntimeError("publication failed; rolled back")
        # Model event: write visibility plus TLB acknowledgement precede version.
        p["map_generation"] += 1
        return p["map_generation"]

    def lease(self, identity, slot):
        if slot not in self.slots or identity not in self.processes:
            raise ValueError("invalid slot/process")
        state = self.slots[slot]
        if state and (state["refs"] or not state["invalidated"]):
            raise RuntimeError("slot still owned or stale TLB")
        self.generation += 1
        p = self.processes[identity]
        token = (slot, self.generation, identity, p["generation"], p["root"])
        self.slots[slot] = {"token": token, "refs": 0, "invalidated": False,
                            "ttbr1": 0}
        return token

    def relocate_root(self, identity, root):
        p = self.processes[identity]
        if root not in p["pages"]:
            raise ValueError("unowned root")
        if any(s and s["token"][2] == identity and s["refs"] for s in self.slots.values()):
            raise RuntimeError("in-flight root")
        p["root"] = root

    def submit(self, token, va, map_generation):
        slot, _, identity, process_generation, root = token
        state = self.slots[slot]
        p = self.processes[identity]
        if (state is None or state["token"] != token or
                process_generation != p["generation"] or root != p["root"] or
                map_generation != p["map_generation"] or va not in p["ptes"]):
            raise RuntimeError("stale root/mapping/slot")
        state["refs"] += 1

    def retire(self, token):
        state = self.slots[token[0]]
        if state is None or state["token"] != token or state["refs"] == 0:
            raise RuntimeError("invalid completion")
        state["refs"] -= 1

    def invalidate(self, token):
        state = self.slots[token[0]]
        if state is None or state["token"] != token or state["refs"]:
            raise RuntimeError("in-flight job")
        state["invalidated"] = True  # Model acknowledged ASID TLBI + sync.


class BrokerV5ContractTests(unittest.TestCase):
    def setUp(self):
        self.b = BrokerSpec()
        self.b.create("P", 0x10000000, {0x10000000, 0x12000000, 0x20000000})
        self.b.create("Q", 0x11000000, {0x11000000, 0x30000000})

    def test_context_zero_and_process_isolation(self):
        frozen = self.b.firmware
        with self.assertRaises(ValueError):
            self.b.lease("P", 0)
        with self.assertRaises(ValueError):
            self.b.update("P", 0x20000, 0x30000000)
        pgen = self.b.update("P", 0x20000, 0x20000000)
        qgen = self.b.update("Q", 0x20000, 0x30000000)
        self.assertEqual((pgen, qgen), (1, 1))
        self.assertNotEqual(self.b.processes["P"]["ptes"],
                            self.b.processes["Q"]["ptes"])
        self.assertEqual(self.b.firmware, frozen)

    def test_slot_reuse_waits_for_job_and_tlb_ack(self):
        generation = self.b.update("P", 0x20000, 0x20000000)
        token = self.b.lease("P", 1)
        self.b.submit(token, 0x20000, generation)
        with self.assertRaises(RuntimeError):
            self.b.lease("Q", 1)
        with self.assertRaises(RuntimeError):
            self.b.invalidate(token)
        self.b.retire(token)
        with self.assertRaises(RuntimeError):
            self.b.lease("Q", 1)
        self.b.invalidate(token)
        replacement = self.b.lease("Q", 1)
        self.assertNotEqual(token, replacement)
        self.assertEqual(self.b.slots[1]["ttbr1"], 0)
        with self.assertRaises(RuntimeError):
            self.b.submit(token, 0x20000, generation)

    def test_update_rollback_never_advances_mapping_fence(self):
        generation = self.b.update("P", 0x20000, 0x20000000)
        before = dict(self.b.processes["P"]["ptes"])
        with self.assertRaises(RuntimeError):
            self.b.update("P", 0x24000, 0x20000000, fail_before_ack=True)
        self.assertEqual(self.b.processes["P"]["ptes"], before)
        self.assertEqual(self.b.processes["P"]["map_generation"], generation)

    def test_root_change_rejects_prior_lease(self):
        generation = self.b.update("P", 0x20000, 0x20000000)
        token = self.b.lease("P", 63)
        self.b.relocate_root("P", 0x12000000)  # VidMm relocation event.
        with self.assertRaises(RuntimeError):
            self.b.submit(token, 0x20000, generation)
        self.b.invalidate(token)
        fresh = self.b.lease("P", 63)
        self.b.submit(fresh, 0x20000, generation)

    def test_inactive_process_count_exceeds_physical_slots(self):
        for index in range(70):
            root = 0x50000000 + index * 0x4000
            self.b.create(f"inactive-{index}", root, {root})
        self.assertEqual(len(self.b.processes), 72)
        self.assertEqual(len(self.b.slots), 63)
        token = self.b.lease("inactive-69", 63)
        self.assertEqual(token[2], "inactive-69")
        self.assertEqual(self.b.slots[63]["ttbr1"], 0)


if __name__ == "__main__":
    unittest.main()
