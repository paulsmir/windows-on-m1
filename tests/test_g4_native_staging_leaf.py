"""RC60: native-staging UpdatePageTable containment, real paging code.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP1981 (Wom1DdiStatus slot 123: UpdatePageTable 0xC000000D for the third
32 MiB native-staging allocation; Wom1G3PrivateAcquireFailure Reserved
0x002605a4 = site 0x26 line 1444) shows the native-binding check in
AdmissionG3UpdateLeaf failing a legal VidMm update, after which
BuildPagingBufferChecked poisoned the whole process and every later private
ACQUIRE and native ring map (D3DDDIERR_INVALIDHANDLE 0x88760873) was refused.
The replay runs the actual AdmissionG3UpdateLeaf, its native-staging helpers
and the actual AdmissionNativeStagingPage/Describe with a host MDL:
- exact 4 KiB, partial and 64 KiB mappings of ring pages publish native leaves;
- a repeated dummy page, ring pages at the wrong offset, an offset past the
  binding, a foreign PFN and a non-system segment all SUCCEED (no poison),
  publish no native leaf for the affected group only, keep VidMm's logical
  page, and are named by the Wom1G3NativePteRefusal receipt;
- ordinary allocations and opaque handles keep the existing rule.
The RED case runs the pre-RC60 UpdateLeaf (ff8a6872) on the same input and
requires it to return a failure for the legal repeated-dummy update.

RC61 (EXP1983, Wom1G3NativePteRefusal reason 4): one VidMm mapping of a
32 MiB native-staging slice crossed a 32 MiB leaf table. The second table's
update (FirstPteVirtualAddress 0x1106000000, StartIndex 0) carried
AllocationOffsetInBytes 0 while its PTEs held the correct ring pages
(ring offset 0x6020000 = BindingOffset 0x4090000 + 0x1f90000, the VA offset);
the KMD derived offset 0 and refused every group, so the readiness proof
returned TailShort and the device was removed. Several arenas and slices are
mapped across a table boundary with that VidMm shape; every native leaf and
logical offset must equal the PFN of the registered pointer (ring UserVa +
binding Offset + VA offset), and pages of another arena or another slice
must stay unpublished. The RC61 RED case runs the RC60 UpdateLeaf (c48591a4)
and requires the continuation and shifted-frame cases to fail.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

from g3_vidmm_replay import body

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers/apple-agx/shared"
RENDER = ROOT / "drivers/apple-agx/render-admission"
PAGING_PATH = "drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c"
BASE = "ff8a6872"
RC60 = "c48591a4"  # last UpdateLeaf before the RC61 frame anchor


def native_types(source):
    magic = re.search(r"(?m)^#define NATIVE_BINDING_MAGIC .*$", source).group(0)
    magic += "\n" + re.search(r"(?m)^#define NATIVE_RING_CHUNK .*$", source).group(0)
    start = source.index("typedef struct _NATIVE_RING {")
    end = source.index("} NATIVE_BINDING;") + len("} NATIVE_BINDING;")
    return magic + "\n" + source[start:end] + "\n"


class NativeStagingLeaf(unittest.TestCase):
    def build_and_run(self, paging):
        native = (RENDER / "src/native_staging_windows.c").read_text()
        private = (RENDER / "src/gpuva_g3_private.h").read_text()
        header = (RENDER / "include/render_admission.h").read_text()
        with tempfile.TemporaryDirectory(prefix="rc60-leaf-") as directory:
            d = Path(directory)
            start = private.index("static inline void AdmissionG3NativeInvalidatePtes(")
            (d / "native_invalidate.inc").write_text(private[start:private.index("\n}", start) + 2])
            types = header[header.index("#define ADMISSION_G3_LEAF_RING "):]
            types = types[:types.index("} ADMISSION_G3_LEAF_HISTORY;") + len("} ADMISSION_G3_LEAF_HISTORY;")]
            (d / "history.inc").write_text(types)
            (d / "native_types.inc").write_text(native_types(native))
            (d / "native.inc").write_text("\n".join(
                body(native, n) for n in ("AdmissionNativeStagingPage",
                                          "AdmissionNativeStagingLocate",
                                          "AdmissionNativeStagingDescribe")))
            names = ["AdmissionG3RecordOmitted", "AdmissionG3RecordUnmapOwners"]
            names += [n for n in ("AdmissionG3NativeStagingBinding",
                                  "AdmissionG3NativeStagingGroup",
                                  "AdmissionG3NativeStagingRebase")
                      if re.search(r"\b" + n + r"\s*\([^;]*\)\s*\{", paging)]
            names.append("AdmissionG3UpdateLeaf")
            (d / "leaf.inc").write_text("\n".join(body(paging, n) for n in names))
            exe = d / "replay"
            build = subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-Wno-unused-function", "-fsanitize=address,undefined",
                "-I", str(d), "-I", str(SHARED / "include"), "-I", str(RENDER / "include"),
                str(ROOT / "tests/g4_native_staging_leaf.c"),
                str(SHARED / "src/apple_agx_gpuva_g3_translation.c"), "-o", str(exe),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            return subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)

    def test_green_legal_updates_are_contained(self):
        run = self.build_and_run((ROOT / PAGING_PATH).read_text())
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("RC60 native staging leaf: PASS", run.stdout)

    def test_decoder_matches_receipt_layout(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "decode_native_pte_refusal", ROOT / "tools/decode_native_pte_refusal.py")
        decoder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(decoder)
        self.assertEqual(decoder.SIZE, 184)  # g4_native_staging_leaf.c asserts Bytes==184
        import struct
        raw = struct.pack("<12I17Q", 1, 184, 5, 1, 0, 32, 0, 0, 0, 10028, 0, 0,
                          *([0] * 15), 8, 1)
        out = decoder.decode(raw)
        self.assertEqual((out["ReasonName"], out["UpdateFlagNames"], out["RefusedGroups"]),
                         ("foreign", ["Repeat"], "0x8"))

    def test_red_pre_rc60_rule_fails_legal_update(self):
        try:
            paging = subprocess.check_output(["git", "show", f"{BASE}:{PAGING_PATH}"],
                                             cwd=ROOT, text=True, stderr=subprocess.DEVNULL)
        except (subprocess.CalledProcessError, FileNotFoundError):
            self.skipTest(f"base revision {BASE} is not available")
        run = self.build_and_run(paging)
        self.assertNotEqual(run.returncode, 0, run.stdout)
        self.assertIn("FAIL repeat-dummy status", run.stdout)
        self.assertIn("FAIL misplaced status", run.stdout)
        self.assertIn("FAIL segment status", run.stdout)

    def test_red_rc60_refuses_continuation_frame(self):
        try:
            paging = subprocess.check_output(["git", "show", f"{RC60}:{PAGING_PATH}"],
                                             cwd=ROOT, text=True, stderr=subprocess.DEVNULL)
        except (subprocess.CalledProcessError, FileNotFoundError):
            self.skipTest(f"base revision {RC60} is not available")
        run = self.build_and_run(paging)
        self.assertNotEqual(run.returncode, 0, run.stdout)
        self.assertIn("FAIL continuation native", run.stdout)
        self.assertIn("FAIL continuation logical offset", run.stdout)
        self.assertIn("FAIL shifted frame publishes every group", run.stdout)
        # RC60 containment itself is unchanged: no update fails.
        self.assertNotIn("status", "".join(
            line for line in run.stdout.splitlines() if line.startswith("FAIL")))


if __name__ == "__main__":
    unittest.main()
