"""Offline tests of tools/ans_repair/ans_hivelog.py.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
An offline registry repair that Windows silently undoes or rejects: a value changed in the
primary file while a pending log entry still carries the old page; a log entry whose hashes no
longer match after its page was edited (replay would stop there and drop the rest of the log);
a replay order that differs from the format rules.  The hash is checked against two entry
headers written by Windows on the bench disk.
"""
import importlib.util
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load(name, rel):
    spec = importlib.util.spec_from_file_location(name, ROOT / rel)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


R = load("ans_offline_recon", "tools/ans_repair/ans_offline_recon.py")
L = load("ans_hivelog", "tools/ans_repair/ans_hivelog.py")
T = load("recon_tests_for_builder", "tests/test_ans_offline_recon.py")
PAGE = 4096
SO = "ControlSet001\\Services\\stornvme\\StartOverride"


def locate(img):
    h = R.Hive(lambda o, n: img[o:o + n], len(img))
    key = h.open(SO)
    v = h.value(key, "0") if key else None
    return None if v is None else v["data_file_offset"]


def entry(seq, bins, pages, flags=0):
    """pages: [(hive-bins offset, 4096 bytes)]"""
    body = b"".join(struct.pack("<II", off, PAGE) for off, _ in pages) + b"".join(d for _, d in pages)
    size = (40 + len(body) + 511) & ~511
    e = bytearray((b"HvLE" + struct.pack("<5I", size, flags, seq, bins, len(pages)) + bytes(16) + body).ljust(size, b"\0"))
    e[24:32] = L.marvin32(bytes(e[40:]))
    e[32:40] = L.marvin32(bytes(e[:32]))
    return bytes(e)


def log_file(primary, first_seq, entries):
    base = bytearray(primary[:512])
    struct.pack_into("<II", base, 4, first_seq, first_seq)
    struct.pack_into("<I", base, 28, 6)
    struct.pack_into("<I", base, 508, L.checksum(bytes(base)))
    return bytes(base) + b"".join(entries) + bytes(4096)


class Marvin(unittest.TestCase):
    def test_hash2_of_entries_written_by_windows(self):
        for hexa in ("48764c4500fe280000000000f037830500b0f901a4010000cc62f9e80b6d806db2f8a2550945b04b",
                     "48764c450060000000000000f137830500b0f901050000009bcb73518c7516c07d63f2233dfa9b04"):
            h = bytes.fromhex(hexa)
            self.assertEqual(L.marvin32(h[:32]), h[32:40])

    def test_tail_lengths_differ(self):
        self.assertEqual(len({L.marvin32(b"abcdefg"[:n]) for n in range(8)}), 8)


class Replay(unittest.TestCase):
    def setUp(self):
        self.clean = T.build_hive((7, 7))
        self.dirty = T.build_hive((8, 7))
        self.bins = len(self.dirty) - PAGE
        self.pos = locate(self.dirty)
        self.page = (self.pos - PAGE) & ~(PAGE - 1)
        self.other = 0 if self.page else PAGE

    def pg(self, off, value=None):
        d = bytearray(self.dirty[PAGE + off:PAGE + off + PAGE])
        if value is not None:
            struct.pack_into("<I", d, self.pos - PAGE - off, value)
        return off, bytes(d)

    def logs(self, *entries, first=7):
        return {".LOG1": log_file(self.dirty, first, entries), ".LOG2": b""}

    def test_clean_primary_ignores_the_log(self):
        order, notes = L.plan_replay(self.clean, {".LOG1": log_file(self.clean, 7, [entry(7, self.bins, [self.pg(self.page, 0)])])})
        self.assertEqual(order, [])
        self.assertIn("clean", notes["primary"])

    def test_entries_apply_in_sequence_and_stop_at_a_gap_or_bad_hash(self):
        e7, e8, e10 = (entry(s, self.bins, [self.pg(self.other)]) for s in (7, 8, 10))
        order, notes = L.plan_replay(self.dirty, self.logs(e7, e8, e10))
        self.assertEqual([e["sequence"] for _, e in order], [7, 8])
        bad = bytearray(e8)
        bad[-1 - 512] ^= 1  # inside the page data
        order, notes = L.plan_replay(self.dirty, self.logs(e7, bytes(bad)))
        self.assertEqual([e["sequence"] for _, e in order], [7])
        self.assertIn("Hash", notes[".LOG1"])
        # a log that starts below the primary's secondary sequence number is not used
        order, _ = L.plan_replay(self.dirty, self.logs(entry(6, self.bins, [self.pg(self.other)]), first=6))
        self.assertEqual(order, [])

    def test_two_logs_continue_each_other(self):
        logs = {".LOG1": log_file(self.dirty, 9, [entry(9, self.bins, [self.pg(self.other)])]),
                ".LOG2": log_file(self.dirty, 7, [entry(7, self.bins, [self.pg(self.page, 5)]), entry(8, self.bins, [self.pg(self.other)])])}
        order, _ = L.plan_replay(self.dirty, logs)
        self.assertEqual([(n, e["sequence"]) for n, e in order], [(".LOG2", 7), (".LOG2", 8), (".LOG1", 9)])
        img, source = L.replay(self.dirty, logs)
        self.assertEqual(struct.unpack_from("<I", img, self.pos)[0], 5)
        self.assertEqual(struct.unpack_from("<II", img, 4), (10, 10))
        self.assertEqual(struct.unpack_from("<I", img, 508)[0], L.checksum(img[:512]))
        self.assertEqual(source[self.page][0], ".LOG2")

    def test_a_primary_only_change_is_undone_by_the_log(self):
        """The defect the planner exists for."""
        logs = self.logs(entry(7, self.bins, [self.pg(self.page)]))
        patched = bytearray(self.dirty)
        struct.pack_into("<I", patched, self.pos, 0)
        img, _ = L.replay(bytes(patched), logs)
        self.assertEqual(struct.unpack_from("<I", img, self.pos)[0], 3)

    def test_plan_changes_every_copy_and_rehashes(self):
        logs = self.logs(entry(7, self.bins, [self.pg(self.other), self.pg(self.page)]),
                         entry(8, self.bins, [self.pg(self.other)]),
                         entry(9, self.bins, [self.pg(self.page)]))
        old, new = struct.pack("<I", 3), struct.pack("<I", 0)
        plan = L.plan_byte_change(self.dirty, logs, locate, old, new)
        files = [(e["file"], e["why"].split(" of ")[0]) for e in plan["edits"]]
        self.assertEqual(sum(1 for e in plan["edits"] if e["file"] == "primary"), 1)
        self.assertEqual(sum(1 for e in plan["edits"] if "copy read" in e["why"]), 3, files)
        self.assertEqual(sorted(e["why"] for e in plan["edits"] if "Hash" in e["why"]),
                         ["Hash-1 and Hash-2 of log entry 7", "Hash-1 and Hash-2 of log entry 9"])
        states = L.verify_plan(self.dirty, logs, plan, locate, old, new)
        self.assertEqual([s["value"] for s in states], ["00000000"] * 4)
        self.assertEqual([s["bytes_changed"] for s in states], [1, 1, 1, 1])
        # every edit's old bytes are what the unpatched file holds, and applying them gives the patched file
        for name, raw in (("primary", self.dirty), (".LOG1", logs[".LOG1"])):
            buf = bytearray(raw)
            for e in plan["edits"]:
                if e["file"] == name:
                    o, n = bytes.fromhex(e["old"]), bytes.fromhex(e["new"])
                    self.assertEqual(bytes(buf[e["offset"]:e["offset"] + len(o)]), o)
                    buf[e["offset"]:e["offset"] + len(n)] = n
            self.assertEqual(bytes(buf), plan["patched_primary"] if name == "primary" else plan["patched_logs"][name])
        # entry 8 was not touched
        e8 = [e for e in plan["entries"] if e["sequence"] == 8][0]
        a, b = logs[".LOG1"], plan["patched_logs"][".LOG1"]
        self.assertEqual(a[e8["log_offset"]:e8["log_offset"] + e8["size"]], b[e8["log_offset"]:e8["log_offset"] + e8["size"]])

    def test_copy_that_already_reads_new_is_left_alone_and_a_third_value_is_refused(self):
        old, new = struct.pack("<I", 3), struct.pack("<I", 0)
        logs = self.logs(entry(7, self.bins, [self.pg(self.page)]), entry(8, self.bins, [self.pg(self.page, 0)]))
        plan = L.plan_byte_change(self.dirty, logs, locate, old, new)
        self.assertEqual(sorted(e["why"] for e in plan["edits"] if "Hash" in e["why"]), ["Hash-1 and Hash-2 of log entry 7"])
        L.verify_plan(self.dirty, logs, plan, locate, old, new)
        logs = self.logs(entry(7, self.bins, [self.pg(self.page, 1)]))
        with self.assertRaises(L.LogError):
            L.plan_byte_change(self.dirty, logs, locate, old, new)

    def test_verify_rejects_a_plan_with_a_stale_hash(self):
        old, new = struct.pack("<I", 3), struct.pack("<I", 0)
        logs = self.logs(entry(7, self.bins, [self.pg(self.page)]))
        plan = L.plan_byte_change(self.dirty, logs, locate, old, new)
        broken = bytearray(plan["patched_logs"][".LOG1"])
        broken[512 + 24] ^= 1
        plan["patched_logs"][".LOG1"] = bytes(broken)
        with self.assertRaises(L.LogError):
            L.verify_plan(self.dirty, logs, plan, locate, old, new)


if __name__ == "__main__":
    unittest.main()
