"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The KMD drains only the firmware event channel; Asahi also drains fw_log,
ktrace and stats on every interrupt (drm/asahi gpu.rs). The Wom1FwRx receipt
must tell whether those rings are full and stuck without changing them:
- it never writes the channel memory (read-only views, every access through
  ReadU32, the read pointer stays as found);
- stats messages decode at the Asahi/m1n1 V13_5 offsets (0x40-byte entries,
  u32 tag, 4-byte aligned u64 payload), the newest entry of each tag wins, and
  a wrapped ring is scanned oldest first from the write pointer;
- "full" is write + 1 == read (mod 0x100), write-pointer changes and wraps are
  counted per sample, an out-of-range pointer is a failure, not a decode.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'

SOURCE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_fw_rx_receipt.h"
static unsigned char stats_state[0x30], ktrace_state[0x30], fwlog_state[6 * 0x30];
static unsigned char ring[0x100 * 0x40];
static unsigned reads, fail_at;
static unsigned rd(void *c, const volatile unsigned *a, unsigned *v) {
  (void)c; ++reads;
  if (fail_at && reads == fail_at) return 0;
  *v = *(const unsigned *)(const void *)a; return 1;
}
static unsigned publishes;
static unsigned pub(void *c, volatile unsigned *a, unsigned v) {
  (void)c; ++publishes; *(unsigned *)(void *)a = v; return 1;
}
static void put(unsigned char *b, unsigned off, unsigned v) { memcpy(b + off, &v, 4); }
static void put64(unsigned char *b, unsigned off, unsigned long long v) { put(b, off, (unsigned)v); put(b, off + 4, (unsigned)(v >> 32)); }
static void msg(unsigned slot, unsigned tag, unsigned long long ts, unsigned a, unsigned b) {
  unsigned char *e = ring + slot * 0x40; memset(e, 0, 0x40);
  put(e, 0, tag); put64(e, 4, ts);
  if (tag == 4) { put(e, 12, a); put(e, 16, b); put(e, 20, a + 1); put(e, 24, b + 1); }
  if (tag == 0xb) { put64(e, 12, ts - 5); put(e, 20, a); put(e, 24, b); put(e, 32, 7); }
  if (tag == 0xc) put(e, 12, a);
  if (tag == 0xd) { put(e, 12, a); put(e, 20, b); }
}
int main(int argc, char **argv) {
  APPLE_AGX_FW_RX_VIEW v = {stats_state, ring, ktrace_state, fwlog_state, rd, 0};
  APPLE_AGX_FW_RX_RECEIPT r;
  unsigned char before[sizeof(ring)];
  AppleAgxFwRxReceiptInit(&r);
  assert(r.Version == 1 && r.Bytes == 304);
  /* 3 messages, not wrapped */
  msg(0, 4, 100, 50, 60); msg(1, 0xc, 110, 900, 0); msg(2, 4, 120, 70, 80);
  put(stats_state, 0x00, 0); put(stats_state, 0x20, 3);
  put(ktrace_state, 0x00, 0); put(ktrace_state, 0x20, 0x1f0);
  for (unsigned i = 0; i < 6; ++i) put(fwlog_state, i * 0x30 + 0x20, 10 + i);
  memcpy(before, ring, sizeof(ring));
  assert(AppleAgxFwRxSample(&r, &v, 1000));
  assert(!memcmp(before, ring, sizeof(ring)));
  assert(r.Samples == 1 && !r.Failures && r.StatsWrite == 3 && r.StatsWriteFirst == 3 && !r.StatsFull);
  assert(r.StatsScanned == 3 && r.StatsTags[4] == 2 && r.StatsTags[0xc] == 1);
  assert(r.UtilTimestamp == 120 && r.Util[0] == 70 && r.Util[1] == 80 && r.Util[2] == 71 && r.Util[3] == 81);
  assert(r.FwBusyTimestamp == 110 && r.FwBusy == 900);
  assert(r.KtraceWrite == 0x1f0 && r.FwlogWrite[5] == 15 && !r.StatsRead);
  /* the firmware filled the ring up to the unread read pointer and stopped */
  for (unsigned s = 3; s < 0xff; ++s) msg(s, s & 1 ? 0xb : 0xd, 200 + s, 1, 2);
  put(stats_state, 0x20, 0xff); put(ktrace_state, 0x20, 0x10);
  assert(AppleAgxFwRxSample(&r, &v, 2000));
  assert(r.StatsFull && r.StatsWriteChanges == 1 && !r.StatsWrapped && r.StatsScanned == 0xff);
  assert(r.KtraceWriteChanges == 1 && r.StatsTags[0xb] + r.StatsTags[0xd] == 0xfc);
  assert(r.PowerStateTimestamp == 200 + 0xfd && r.PowerStateLastBusy == 200 + 0xfd - 5 &&
         r.PowerStateActive == 1 && r.PowerStateOff == 2 && r.PowerStatePstate == 7);
  assert(r.PStateMin == 1 && r.PStateMax == 2 && r.FirstQpc == 1000 && r.LastQpc == 2000);
  /* unchanged pointer: no change counted */
  assert(AppleAgxFwRxSample(&r, &v, 3000) && r.StatsWriteChanges == 1 && r.Samples == 3);
  /* an overwriting firmware wraps: scanned oldest first from the write pointer */
  msg(0xff, 0xc, 9000, 1234, 0); msg(0, 0xc, 9100, 4321, 0); msg(1, 0x2a, 0, 0, 0);
  put(stats_state, 0x20, 2);
  assert(AppleAgxFwRxSample(&r, &v, 4000));
  assert(r.StatsWrapped && r.StatsScanned == 0x100 && r.StatsUnknownTags == 1 && r.FwBusy == 4321);
  /* a pointer out of range is a failure, not a decode */
  put(stats_state, 0x20, 0x100);
  assert(!AppleAgxFwRxSample(&r, &v, 5000) && r.Failures == 1 && r.StatsWrite == 2);
  /* a failed read is counted */
  put(stats_state, 0x20, 2); reads = 0; fail_at = 1;
  assert(!AppleAgxFwRxSample(&r, &v, 6000) && r.Failures == 2);
  fail_at = 0;
  /* no stats ring view: pointers only */
  v.StatsRing = 0;
  assert(AppleAgxFwRxSample(&r, &v, 7000));
  /* Wom1FwRxDrain: read_ptr := write_ptr, nothing else written */
  {
    static unsigned char fwlog_before[sizeof(fwlog_state)];
    unsigned char stats_before[sizeof(stats_state)], ktrace_before[sizeof(ktrace_state)];
    APPLE_AGX_FW_RX_RECEIPT d;
    AppleAgxFwRxReceiptInit(&d);
    put(stats_state, 0x00, 0); put(stats_state, 0x20, 0xff);
    put(ktrace_state, 0x00, 0x1f0); put(ktrace_state, 0x20, 0x10);
    for (unsigned i = 0; i < 6; ++i) { put(fwlog_state, i * 0x30, 0); put(fwlog_state, i * 0x30 + 0x20, i ? 10 + i : 0); }
    memcpy(stats_before, stats_state, sizeof(stats_state));
    memcpy(ktrace_before, ktrace_state, sizeof(ktrace_state));
    memcpy(fwlog_before, fwlog_state, sizeof(fwlog_state));
    APPLE_AGX_FW_RX_DRAIN_VIEW dv = {stats_state, ktrace_state, fwlog_state, rd, pub, 0};
    publishes = 0;
    assert(AppleAgxFwRxDrain(&d, &dv));
    assert(d.DrainCalls == 1 && !d.DrainFailures && d.DrainedStats == 0xff);
    assert(d.DrainedKtrace == 0x20 /* 0x1f0 -> 0x10 wraps at 0x200 */);
    assert(d.DrainedFwlog == 11 + 12 + 13 + 14 + 15 && publishes == 1 + 1 + 5);
    unsigned rp; memcpy(&rp, stats_state, 4); assert(rp == 0xff);
    memcpy(&rp, ktrace_state, 4); assert(rp == 0x10);
    memcpy(&rp, fwlog_state + 5 * 0x30, 4); assert(rp == 15);
    /* only bytes 0..3 of each state changed */
    assert(!memcmp(stats_state + 4, stats_before + 4, sizeof(stats_state) - 4));
    assert(!memcmp(ktrace_state + 4, ktrace_before + 4, sizeof(ktrace_state) - 4));
    for (unsigned i = 0; i < 6; ++i)
      assert(!memcmp(fwlog_state + i * 0x30 + 4, fwlog_before + i * 0x30 + 4, 0x2c));
    /* already drained: no publish */
    publishes = 0;
    assert(AppleAgxFwRxDrain(&d, &dv) && !publishes && d.DrainCalls == 2 && d.DrainedStats == 0xff);
    /* an out-of-range pointer is left alone and counted */
    put(fwlog_state, 3 * 0x30 + 0x20, 0x100);
    assert(!AppleAgxFwRxDrain(&d, &dv) && d.DrainFailures == 1);
    memcpy(&rp, fwlog_state + 3 * 0x30, 4); assert(rp == 13);
    put(fwlog_state, 3 * 0x30 + 0x20, 13);
  }
  if (argc > 1) { FILE *f = fopen(argv[1], "wb"); fwrite(&r, sizeof(r), 1, f); fclose(f); }
  return 0;
}
'''


class FwRxReceipt(unittest.TestCase):
    def test_read_only_sample_decodes_stats_and_pointer_states(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'rx.c'
            exe = Path(tmp) / 'rx'
            src.write_text(SOURCE)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I', str(SHARED / 'include'), str(src),
                            str(SHARED / 'src/apple_agx_fw_rx_receipt.c'), '-o', str(exe)], check=True)
            out = Path(tmp) / 'rx.bin'
            subprocess.run([str(exe), str(out)], check=True, timeout=10)
            import importlib.util
            spec = importlib.util.spec_from_file_location('decode_fw_rx', ROOT / 'tools/decode_fw_rx.py')
            decoder = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(decoder)
            d = decoder.decode(out.read_bytes())
            self.assertEqual((d['version'], d['bytes'], d['samples'], d['failures']), (1, 304, 7, 2))
            self.assertEqual((d['stats_write'], d['stats_wrapped'], d['fw_busy']), (2, 1, 4321))
            self.assertEqual((d['power_state_pstate'], d['pstate_max'], d['last_qpc']), (7, 2, 7000))
            self.assertEqual(d['fwlog_write'], [10, 11, 12, 13, 14, 15])
            self.assertEqual(d['verdict'], 'stats ring moving')
            self.assertEqual((d['drain_calls'], d['drain_failures']), (0, 0))

    def test_ring_that_filled_during_sampling_is_stuck(self):
        """EXP2216 after-d: write 20 -> 255 in one sample, then full; read 0."""
        import importlib.util
        import struct
        spec = importlib.util.spec_from_file_location('decode_fw_rx', ROOT / 'tools/decode_fw_rx.py')
        decoder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(decoder)
        fields = [0] * len(struct.unpack(decoder.FMT, bytes(304)))
        fields[0:10] = [1, 304, 78, 0, 0, 255, 20, 1, 0, 1]
        d = decoder.decode(struct.pack(decoder.FMT, *fields))
        self.assertEqual(d['verdict'], 'stats ring full and stuck (values stale)')
        fields[9] = 0
        self.assertEqual(decoder.decode(struct.pack(decoder.FMT, *fields))['verdict'], 'stats ring moving')

    def test_source_never_writes_channel_memory(self):
        text = (SHARED / 'src/apple_agx_fw_rx_receipt.c').read_text()
        sample = text[text.index('unsigned int AppleAgxFwRxSample('):text.index('/* One channel: read_ptr')]
        self.assertNotIn('PublishU32', sample)
        drain = text[text.index('static unsigned int fw_rx_drain_one('):]
        # the drain writes only the read pointer
        self.assertEqual(drain.count('PublishU32('), 1)
        self.assertIn('(state + APPLE_AGX_FW_RX_READ_OFFSET), write)', drain)
        self.assertNotRegex(text, r'\bv->(Stats|Ktrace|Fwlog)\w*\s*\[[^\]]*\]\s*=')
        header = (SHARED / 'include/apple_agx_fw_rx_receipt.h').read_text()
        for view in ('StatsState', 'StatsRing', 'KtraceState', 'FwlogState'):
            self.assertIn('const volatile unsigned char *%s;' % view, header)

    def test_kmd_samples_and_exports_only_with_job_diagnostics(self):
        kmd = (ROOT / 'drivers/apple-agx/render-admission/src/backend_platform_windows.c').read_text()
        sample = kmd[kmd.index('static VOID AdmissionFwRxSampleWindows('):]
        sample = sample[:sample.index('\n}\n')]
        self.assertIn('if (runtime == NULL || !runtime->JobDiagnostics) return;', sample)
        self.assertIn('view.ReadU32 = AdmissionFwRxReadWindows;', sample)
        self.assertNotIn('PublishU32', sample)
        export = kmd[kmd.index('static VOID AdmissionJobTimingExportWindows('):]
        export = export[:export.index('\n}\n')]
        # sampled after the diagnostics gate and the two-second throttle
        self.assertLess(export.index('runtime->JobTimingExportQpc = before;'),
                        export.index('AdmissionFwRxSampleWindows(runtime, before);'))
        self.assertIn('RtlInitUnicodeString(&name, L"Wom1FwRx");', export)
        drain = kmd[kmd.index('static VOID AdmissionFwRxDrainWindows('):]
        drain = drain[:drain.index('\n}\n')]
        self.assertIn('!runtime->FwRxDrain', drain)
        self.assertIn('runtime->FwRxDrain =\n      AdmissionPlatformSwitchEnabled(Context, L"Wom1FwRxDrain");', kmd)
        self.assertIn('if (runtime->FwRxDrain) AdmissionFwRxDrainWindows(runtime);', kmd)
        project = (ROOT / 'drivers/apple-agx/render-admission/AppleAgxRenderAdmission.vcxproj').read_text()
        self.assertIn('..\\shared\\src\\apple_agx_fw_rx_receipt.c', project)


if __name__ == '__main__':
    unittest.main()
