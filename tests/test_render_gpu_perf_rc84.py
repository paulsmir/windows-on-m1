"""rc84: GPU performance-state receipt (Wom1GpuPerf) from the live HwDataA.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The rc84 question is whether the GPU runs at its top performance state
(state 6, 1278 MHz on J313) while GravityMark keeps it busy. The only
existing evidence is the per-job pstate word of Wom1JobTiming971, which is
written for render jobs only and was exported after the benchmark had
ended (its 64 slots are the benchmark's last 2.5-3.3 s: EXP2010/EXP2011).
The receipt must:
- read the firmware-written HwDataA fields at the offsets of the G13 V13_5
  layout (Asahi fw/initdata.rs HwDataA, m1n1 AGXHWDataA: pwr_status 0x10,
  actual_pstate 0x2c, tgt_pstate 0x30, cur_pstate 0x38, freq_mhz 0x58,
  temp_c 0x1c4, avg_power_mw 0x1c8, update_ts 0x1cc, cur_power_mw_2 0x740,
  perf_cur_utilization 0x7e8), and refuse a short or missing object;
- attribute the QPC time between two samples to the state seen at the
  second one, excluding gaps over ADMISSION_GPU_PERF_GAP_MS (idle desktop,
  benchmark loading), so the histogram is the time share of each state;
- count samples where the firmware's actual state is below its target
  (a power/thermal limiter: EXP2011 showed actual 4 / target 5);
- count firmware updates of update_ts (a never-changing value means the
  fields are not live and the histogram is meaningless);
- keep out-of-range states out of the histograms;
- keep the decoder's frequency labels equal to the J313 ADT table that
  the RegionC encoder validates;
- be wired into every job completion, exported with the job-timing
  receipt, and disabled only by Wom1GpuPerfReceipt = 0.
"""
from pathlib import Path
import importlib.util
import os
import re
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "drivers/apple-agx/render-admission/include"
HEADER = INCLUDE / "render_gpu_perf.h"
BACKEND = ROOT / "drivers/apple-agx/render-admission/src/backend_platform_windows.c"
REGIONC = ROOT / "drivers/apple-agx/shared/src/apple_agx_regionc.c"
DECODER = ROOT / "tools/agx_gpu_perf_decode.py"


def function_body(source, name):
    matches = list(re.finditer(r"(?m)^[A-Za-z_][A-Za-z0-9_ \t*]*\b" + name + r"\s*\([^;{]*?\)\s*\{",
                               source, re.S))
    if not matches:
        raise AssertionError(f"missing production function {name}")
    match = matches[-1]
    depth, pos = 1, match.end()
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[match.start():pos]


def build_and_run(test, program, args=()):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "model.c"
        exe = Path(directory) / "model"
        path.write_text(program)
        built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                                "-Werror", "-fsanitize=address,undefined", "-I", str(INCLUDE),
                                str(path), "-o", str(exe)], capture_output=True, text=True)
        test.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        ran = subprocess.run([str(exe), *args], capture_output=True, text=True)
        test.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        return ran.stdout


MODEL = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "render_gpu_perf.h"

static _Alignas(8) unsigned char hw[0x421c];
static void put(unsigned off, unsigned v) { memcpy(hw + off, &v, 4); }
static void put64(unsigned off, unsigned long long v) { memcpy(hw + off, &v, 8); }
static void state(unsigned a, unsigned t, unsigned c) {
  put(0x2c, a); put(0x30, t); put(0x38, c);
}
static ADMISSION_GPU_PERF_STATE s;

int main(int argc, char **argv) {
  ADMISSION_GPU_PERF_SAMPLE x;
  const unsigned long long f = 24000000ull, ms = 24000ull;
  /* decoder layout '<IIII4Q16Q48I8I' + 32 x '<QQ8I' */
  assert(ADMISSION_GPU_PERF_VERSION == 1u && ADMISSION_GPU_PERF_PSTATES == 16u &&
         ADMISSION_GPU_PERF_RING == 32u && ADMISSION_GPU_PERF_GAP_MS == 50u);
  assert(sizeof(ADMISSION_GPU_PERF_SAMPLE) == 48u);
  assert(offsetof(ADMISSION_GPU_PERF_STATE, ActualTicks) == 48u);
  assert(offsetof(ADMISSION_GPU_PERF_STATE, ActualCount) == 176u);
  assert(offsetof(ADMISSION_GPU_PERF_STATE, LimitedCount) == 368u);
  assert(offsetof(ADMISSION_GPU_PERF_STATE, Ring) == 400u);
  assert(sizeof(ADMISSION_GPU_PERF_STATE) == 400u + 32u * 48u);
  /* G13 V13_5 HwDataA offsets (Asahi fw/initdata.rs, m1n1 AGXHWDataA) */
  assert(ADMISSION_GPU_PERF_HWDATAA_PWR_STATUS == 0x10u &&
         ADMISSION_GPU_PERF_HWDATAA_ACTUAL_PSTATE == 0x2cu &&
         ADMISSION_GPU_PERF_HWDATAA_TGT_PSTATE == 0x30u &&
         ADMISSION_GPU_PERF_HWDATAA_CUR_PSTATE == 0x38u &&
         ADMISSION_GPU_PERF_HWDATAA_FREQ_MHZ == 0x58u &&
         ADMISSION_GPU_PERF_HWDATAA_TEMP_C == 0x1c4u &&
         ADMISSION_GPU_PERF_HWDATAA_AVG_POWER_MW == 0x1c8u &&
         ADMISSION_GPU_PERF_HWDATAA_UPDATE_TS == 0x1ccu &&
         ADMISSION_GPU_PERF_HWDATAA_CUR_POWER_MW == 0x740u &&
         ADMISSION_GPU_PERF_HWDATAA_PERF_UTIL == 0x7e8u);

  /* refusals: no object, short object, no output */
  assert(!AdmissionGpuPerfRead(NULL, sizeof(hw), 1ull, &x));
  assert(!AdmissionGpuPerfRead(hw, 0x7e8u + 3u, 1ull, &x));
  assert(!AdmissionGpuPerfRead(hw, sizeof(hw), 1ull, NULL));
  assert(AdmissionGpuPerfRead(hw, 0x7e8u + 4u, 1ull, &x));

  put(0x10, 4u); put(0x58, 0x449fc000u); /* 1278.0f */ put(0x1c4, 61u);
  put(0x1c8, 14500u); put64(0x1cc, 0x1122334455667788ull); put(0x740, 16000u);
  put(0x7e8, 0x42c80000u); /* 100.0f */ state(6u, 6u, 6u);
  assert(AdmissionGpuPerfRead(hw, sizeof(hw), 777ull, &x));
  assert(x.Qpc == 777ull && x.UpdateTs == 0x1122334455667788ull &&
         x.Pstates == (6u | 6u << 8 | 6u << 16) && x.FreqMhzBits == 0x449fc000u &&
         x.TempC == 61u && x.AvgPowerMw == 14500u && x.CurPowerMw == 16000u &&
         x.UtilBits == 0x42c80000u && x.PwrStatus == 4u);
  /* PT1: the set-point words are read back; an object that ends before
   * the second word reports none. */
  assert(ADMISSION_GPU_PERF_HWDATAA_PERF_TGT == 0x788u &&
         ADMISSION_GPU_PERF_HWDATAA_PERF_TGT_2 == 0x7ecu);
  assert(x.Reserved == 0u);
  put(0x788, 60u); put(0x7ec, 85u);
  assert(AdmissionGpuPerfRead(hw, sizeof(hw), 778ull, &x));
  assert(x.Reserved == (60u | 85u << 8));
  assert((x.Reserved & (ADMISSION_GPU_PERF_TGT_APPLIED | ADMISSION_GPU_PERF_TGT_IGNORED)) == 0u);
  assert(AdmissionGpuPerfRead(hw, 0x7ecu + 3u, 778ull, &x) && x.Reserved == 0u);
  put(0x788, 0u); put(0x7ec, 0u);

  AdmissionGpuPerfInitialize(&s, f);
  assert(s.Version == 1u && s.Bytes == sizeof(s) && s.QpcFrequency == f &&
         s.Samples == 0u && s.TempMin == 0xffffffffu);
  AdmissionGpuPerfRecord(NULL, &x);
  AdmissionGpuPerfRecord(&s, NULL);
  assert(s.Samples == 0u);

  /* first sample: counted, no interval yet */
  x.Qpc = 1000ull; AdmissionGpuPerfRecord(&s, &x);
  assert(s.Samples == 1u && s.ActualCount[6] == 1u && s.SampledTicks == 0ull &&
         s.UpdateChanges == 0u);
  /* 10 ms at state 6, firmware update_ts moved */
  x.Qpc = 1000ull + 10u * ms; x.UpdateTs += 8u * ms; AdmissionGpuPerfRecord(&s, &x);
  assert(s.ActualTicks[6] == 10u * ms && s.SampledTicks == 10u * ms && s.UpdateChanges == 1u);
  /* 5 ms limited: actual 4 below target 5, same update_ts */
  put(0x2c, 4u); put(0x30, 5u); put(0x38, 5u); put(0x1c4, 83u);
  assert(AdmissionGpuPerfRead(hw, sizeof(hw), 1000ull + 15u * ms, &x));
  x.UpdateTs = 0x1122334455667788ull + 8u * ms;
  AdmissionGpuPerfRecord(&s, &x);
  assert(s.ActualTicks[4] == 5u * ms && s.TargetCount[5] == 1u && s.CurCount[5] == 1u &&
         s.LimitedCount == 1u && s.LimitedTicks == 5u * ms && s.UpdateChanges == 1u &&
         s.TempMax == 83u && s.TempMin == 61u);
  /* exactly the gap limit is time; one tick more is a gap */
  x.Qpc += ADMISSION_GPU_PERF_GAP_MS * ms; AdmissionGpuPerfRecord(&s, &x);
  assert(s.ActualTicks[4] == (5u + ADMISSION_GPU_PERF_GAP_MS) * ms && s.Gaps == 0u);
  x.Qpc += ADMISSION_GPU_PERF_GAP_MS * ms + 1u; AdmissionGpuPerfRecord(&s, &x);
  assert(s.ActualTicks[4] == (5u + ADMISSION_GPU_PERF_GAP_MS) * ms && s.Gaps == 1u);
  /* a clock that went backwards adds no time */
  x.Qpc -= 5u; AdmissionGpuPerfRecord(&s, &x);
  assert(s.ActualTicks[4] == (5u + ADMISSION_GPU_PERF_GAP_MS) * ms && s.Gaps == 1u);
  /* out-of-range state: ring only */
  x.Pstates = 0x20u | 6u << 8 | 6u << 16; x.Qpc += ms; AdmissionGpuPerfRecord(&s, &x);
  assert(s.Invalid == 1u && s.ActualCount[0] == 0u && s.TargetCount[6] == 2u &&
         s.LimitedCount == 4u);
  /* maxima */
  assert(s.AvgPowerMax == 14500u && s.CurPowerMax == 16000u && s.FreqMhzBitsMax == 0x449fc000u);
  /* a negative float never becomes the maximum */
  x.FreqMhzBits = 0xbf800000u; x.Pstates = 6u | 6u << 8 | 6u << 16; x.Qpc += ms;
  AdmissionGpuPerfRecord(&s, &x);
  assert(s.FreqMhzBitsMax == 0x449fc000u);
  /* the ring keeps the newest 32 samples in Samples % 32 order */
  { unsigned i, before = s.Samples;
    for (i = 0; i < 40u; ++i) { x.Qpc += ms; x.TempC = 100u + i; AdmissionGpuPerfRecord(&s, &x); }
    assert(s.Samples == before + 40u);
    assert(s.Ring[(s.Samples - 1u) % ADMISSION_GPU_PERF_RING].TempC == 139u);
  }
  if (argc > 1) {
    FILE *out = fopen(argv[1], "wb");
    assert(out && fwrite(&s, sizeof(s), 1, out) == 1 && fclose(out) == 0);
  }
  return 0;
}
'''


def load_decoder():
    spec = importlib.util.spec_from_file_location("agx_gpu_perf_decode", DECODER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class GpuPerfReceipt(unittest.TestCase):
    def test_header_is_kmd_safe(self):
        text = HEADER.read_text()
        for banned in ("<stdint.h>", "<inttypes.h>", "<stdio.h>", "<stdlib.h>", "<string.h>"):
            self.assertNotIn(banned, text)
        # no floating-point types: the KMD links without _fltused (EXP1102)
        code = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        self.assertIsNone(re.search(r"\b(float|double)\b", code))

    def test_sampler_model(self):
        build_and_run(self, MODEL)

    def test_decoder_reads_the_c_layout(self):
        with tempfile.TemporaryDirectory() as directory:
            blob = Path(directory) / "Wom1GpuPerf.bin"
            build_and_run(self, MODEL, (str(blob),))
            decoded = load_decoder().decode(blob.read_bytes())
        self.assertEqual(decoded["version"], 1)
        self.assertEqual(decoded["update_changes"], 1)
        self.assertEqual(decoded["limited_count"], 4)
        self.assertEqual(decoded["gaps"], 1)
        self.assertEqual(decoded["invalid"], 1)
        self.assertAlmostEqual(decoded["actual_ms"][6], 10.0 + 1 + 40, places=3)
        self.assertAlmostEqual(decoded["actual_ms"][4], 55.0, places=3)
        self.assertAlmostEqual(decoded["limited_ms"], 55.0, places=3)
        self.assertAlmostEqual(decoded["freq_mhz_max"], 1278.0)
        self.assertEqual(decoded["temp_c"], [61, 139])
        self.assertEqual(decoded["ring"][-1]["temp_c"], 139)
        self.assertEqual(decoded["ring"][-1]["actual"], 6)
        self.assertEqual(len(decoded["ring"]), 32)
        self.assertLess(decoded["ring"][0]["qpc"], decoded["ring"][-1]["qpc"])

    def test_decoder_since_subtracts_an_earlier_snapshot(self):
        with tempfile.TemporaryDirectory() as directory:
            blob = Path(directory) / "Wom1GpuPerf.bin"
            build_and_run(self, MODEL, (str(blob),))
            module = load_decoder()
            later = module.decode(blob.read_bytes())
        earlier = dict(later, samples=10, invalid=0, gaps=1, update_changes=1, sampled_ms=60.0,
                       limited_ms=55.0, limited_count=4,
                       actual_ms=[0.0] * 4 + [55.0, 0.0, 5.0] + [0.0] * 9,
                       actual_count=[0] * 16, target_count=[0] * 16, current_count=[0] * 16)
        delta = module.since(later, earlier)
        self.assertEqual(delta["samples"], later["samples"] - 10)
        self.assertAlmostEqual(delta["actual_ms"][6], 46.0, places=3)
        self.assertAlmostEqual(delta["actual_ms"][4], 0.0, places=3)
        self.assertEqual(delta["limited_count"], 0)
        with self.assertRaises(ValueError):
            module.since(earlier, later)

    def test_decoder_frequencies_are_the_j313_table(self):
        text = REGIONC.read_text()
        table = re.search(r"AppleAgxRegionCJ313Frequencies\[7\] = \{(.*?)\};", text, re.S).group(1)
        hz = [int(v) for v in re.findall(r"(\d+)u", table)]
        self.assertEqual(load_decoder().J313_PSTATE_MHZ, [v // 1000000 for v in hz])

    def test_windows_wiring(self):
        text = BACKEND.read_text()
        self.assertIn('#include "render_gpu_perf.h"', text)
        sample = function_body(text, 'AdmissionGpuPerfSampleWindows')
        self.assertIn('if (runtime->GpuPerfOff) return;', sample)
        self.assertIn('AppleAgxRegionBMemoryHwdataA', sample)
        read = sample.index('AdmissionGpuPerfRead(')
        lock = sample.index('KeAcquireSpinLock(&runtime->JobTimingLock')
        self.assertLess(read, lock)  # firmware memory is read outside the spin lock
        self.assertIn('AdmissionGpuPerfRecord(&runtime->GpuPerf', sample[lock:])
        complete = function_body(text, 'AdmissionBackendComplete')
        self.assertLess(complete.index('AdmissionJobTimingFirmwareWindows(runtime, Fence);'),
                        complete.index('AdmissionGpuPerfSampleWindows(runtime);'))
        start = text.index('AdmissionJobTimingInitialize(&runtime->JobTiming,')
        self.assertIn('AdmissionGpuPerfInitialize(&runtime->GpuPerf,', text[start:start + 400])
        self.assertIn('runtime->GpuPerfOff =\n      AdmissionPlatformSwitchDisabled(Context, L"Wom1GpuPerfReceipt");',
                      text)
        export = function_body(text, 'AdmissionJobTimingExportWindows')
        copy = export.index('RtlCopyMemory(&runtime->GpuPerfSnapshot, &runtime->GpuPerf,')
        # the snapshot is copied while JobTimingLock is held
        # (with the job-timing snapshot, after the rate-limit early return)
        held = export.index('runtime->JobTimingExportQpc = before;')
        release = 'KeReleaseSpinLock(&runtime->JobTimingLock, oldIrql);'
        self.assertLess(held, copy)
        self.assertEqual(export.find(release, held, copy), -1)
        self.assertNotEqual(export.find(release, copy), -1)
        self.assertIn('L"Wom1GpuPerf"', export)
        self.assertIn('runtime->GpuPerfSnapshot.Samples != 0u', export)


if __name__ == "__main__":
    unittest.main()
