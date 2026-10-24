"""T7d/T7f: real leaf safety plus a separately labeled shadow design model.

The model is an executable specification, NOT a KMD shadow implementation.
Its assumed CPU leases, reserved storage and source pins do not exist in the
production path. A positive model result cannot claim actual G4 admission.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_g4_submit_virtual_replay import function_body
from aperture_shadow_model import ShadowEpoch, PAGE, NATIVE

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers/apple-agx/shared"
PAGING = ROOT / "drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c"


class ApertureNativeLeaf(unittest.TestCase):
    def test_scattered_replacement_retires_native_leaf_and_preserves_logical_pages(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        Publishing four scattered PFNs as one native leaf aliases unrelated
        memory. Keeping the old leaf on replacement exposes stale backing.
        The real KMD must invalidate it, preserve all logical PFNs for CPU
        paging, and republish only after a complete contiguous group returns.
        Contiguous but misaligned groups must also stay unpublished; CPU-only
        paging must retain scattered logical pages without GPU publications.
        The execution gate bitmap must follow omissions, repairs and unmaps
        transactionally. This is safety, not native scattered backing.
        """
        with tempfile.TemporaryDirectory(prefix="t7d-leaf-") as directory:
            directory = Path(directory)
            (directory / "leaf.inc").write_text(
                function_body(PAGING.read_text(), "AdmissionG3RecordOmitted") + "\n" +
                function_body(PAGING.read_text(), "AdmissionG3UpdateLeaf") + "\n" +
                function_body(PAGING.read_text(), "AdmissionG3RetireSystemSubtree") + "\n" +
                function_body(PAGING.read_text(), "AdmissionG3ResetTableShadow"))
            binary = directory / "replay"
            build = subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", "-I", str(directory),
                "-I", str(SHARED / "include"),
                "-I", str(ROOT / "drivers/apple-agx/render-admission/include"),
                str(ROOT / "tests/g4_aperture_native_leaf.c"),
                str(SHARED / "src/apple_agx_gpuva_g3_translation.c"),
                "-o", str(binary),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True,
                                 timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("T7d native-leaf safety: PASS", run.stdout)


class ShadowDesignContract(unittest.TestCase):
    """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

    A proposed bounce design must preserve the latest bytes across ownership
    changes. These model checks expose stale copy-in, missing copy-back, aliases
    with independent authority, early fences and freeing uncertain backing.
    They do not test Windows callbacks or prove those transitions are available.
    """

    def epoch(self):
        # Different contents per system page expose copy order/offset mistakes.
        pages = [bytearray((i * 41 + j) & 255 for j in range(PAGE))
                 for i in range(4)]
        return ShadowEpoch(pages)

    def activate(self, epoch):
        self.assertTrue(epoch.prepare(reserved_bytes=NATIVE))
        epoch.copy_in()
        epoch.publish("va-a")
        epoch.signal_ready()

    def test_scattered_copy_publish_gpu_write_evict_copy_back(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Lost GPU writes on eviction."""
        epoch = self.epoch()
        self.assertTrue(epoch.prepare(reserved_bytes=NATIVE))
        # An earlier queued fill executes AFTER prepare and BEFORE copy-in.
        epoch.pages[2][:] = bytes([0xB7]) * PAGE
        expected = b"".join(epoch.pages)
        epoch.copy_in()
        epoch.publish("va-a")
        with self.assertRaises(RuntimeError):
            epoch.gpu_read("va-a")  # Paging fence has not completed.
        epoch.signal_ready()
        self.assertEqual(epoch.gpu_read("va-a"), expected)
        epoch.gpu_write("va-a", PAGE - 3, b"cross-page-write")
        expected = expected[:PAGE-3] + b"cross-page-write" + expected[PAGE+13:]
        epoch.retire(revoke_ack=True)
        self.assertEqual(b"".join(epoch.pages), expected)
        self.assertEqual(epoch.events, ["reserve", "copy-in", "publish",
            "ready-fence", "gpu-write", "revoke-ack", "copy-out",
            "retire-fence", "release"])
        self.assertFalse(epoch.source_held)
        self.assertIsNone(epoch.shadow)

    def test_alias_uses_authoritative_shadow_without_recopy(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? An alias overwrites GPU results."""
        epoch = self.epoch()
        self.activate(epoch)
        epoch.gpu_write("va-a", 11, b"fresh")
        epoch.publish("va-b")
        self.assertEqual(epoch.gpu_read("va-b")[11:16], b"fresh")
        with self.assertRaises(RuntimeError):
            epoch.copy_in()
        epoch.retire(revoke_ack=True)
        self.assertEqual(epoch.pages[0][11:16], b"fresh")
        self.assertFalse(epoch.aliases)

    def test_cpu_lease_excludes_shadow_and_drains_before_pointer(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Divergent persistent CPU pointers."""
        epoch = self.epoch()
        epoch.cpu_begin()
        epoch.cpu_begin()  # Nested map keeps the underlying pointer live.
        self.assertFalse(epoch.prepare(reserved_bytes=NATIVE))
        epoch.cpu_end()
        self.assertFalse(epoch.prepare(reserved_bytes=NATIVE))
        epoch.cpu_end()
        self.activate(epoch)
        epoch.gpu_write("va-a", 0, b"gpu")
        epoch.cpu_begin()
        self.assertEqual(epoch.pages[0][:3], b"gpu")
        self.assertFalse(epoch.aliases)
        self.assertEqual(epoch.cpu_leases, 1)
        self.assertFalse(epoch.prepare(reserved_bytes=NATIVE))

    def test_capacity_refusal_has_no_publication_or_successful_fence(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Late allocation failure after admission."""
        epoch = self.epoch()
        before = b"".join(epoch.pages)
        self.assertFalse(epoch.prepare(reserved_bytes=NATIVE-1))
        self.assertEqual(epoch.events, [])
        self.assertEqual(b"".join(epoch.pages), before)
        self.assertFalse(epoch.source_held)
        with self.assertRaises(RuntimeError):
            epoch.publish("va-a")
        with self.assertRaises(RuntimeError):
            epoch.signal_ready()

    def test_no_publication_before_copy_or_free_after_uncertain_revoke(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Uninitialized or freed GPU backing."""
        epoch = self.epoch()
        self.assertTrue(epoch.prepare(reserved_bytes=NATIVE))
        with self.assertRaises(RuntimeError):
            epoch.publish("va-a")
        epoch.copy_in()
        epoch.publish("va-a")
        epoch.signal_ready()
        epoch.gpu_write("va-a", 0, b"pending")
        before = b"".join(epoch.pages)
        with self.assertRaises(RuntimeError):
            epoch.retire(revoke_ack=False)
        self.assertTrue(epoch.source_held)
        self.assertIsNotNone(epoch.shadow)
        self.assertEqual(b"".join(epoch.pages), before)
        self.assertNotIn("retire-fence", epoch.events)
        self.assertNotIn("release", epoch.events)
        with self.assertRaises(RuntimeError):
            epoch.cpu_begin()
        with self.assertRaises(RuntimeError):
            epoch.gpu_read("va-a")
