"""Executable RC45 whole-held-set readiness transport regressions."""
import unittest

import test_g4_native_allocation_guard as guard
from test_g4_mesa_pool_residency_replay import body


class NativeQueryBatch(unittest.TestCase):
    def test_batch_preserves_all_native_identity_and_extent_guards(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Batching must not
        authorize an omitted interior page, foreign allocation, stale native
        address, short caller prefix, wrong context, or a read-only write.
        """
        source = guard.KMD.read_text()
        self.assertTrue('AdmissionGpuvaG3NativeQueryBatchEscape(' in source,
                      'per-BO escape transport has no whole-held-set batch')
        funcs = '\n'.join(body(source, n) for n in (
            'AdmissionG3CopyPte', 'AdmissionGpuvaG3NativeQueryEscape',
            'AdmissionGpuvaG3NativeQueryBatchEscape'))
        wrapper = r'''
static NTSTATUS batch_one(ADMISSION_CONTEXT *a, DXGKARG_ESCAPE *e) {
 struct { APPLE_AGX_G3_NATIVE_QUERY_BATCH Header; APPLE_AGX_G3_NATIVE_QUERY Entries[1]; } b = {0};
 DXGKARG_ESCAPE request = *e;
 b.Header.Magic=APPLE_AGX_G3_NATIVE_QUERY_BATCH_MAGIC;
 b.Header.Version=APPLE_AGX_G3_NATIVE_QUERY_BATCH_VERSION;
 b.Header.Bytes=sizeof(b);b.Header.Count=1;
 memcpy(b.Entries,e->pPrivateDriverData,sizeof(b.Entries));
 request.pPrivateDriverData=&b;
 request.PrivateDriverDataSize=e->PrivateDriverDataSize==sizeof(b.Entries) ? sizeof(b) : sizeof(b)-1;
 NTSTATUS status=AdmissionGpuvaG3NativeQueryBatchEscape(a,&request);
 memcpy(e->pPrivateDriverData,b.Entries,sizeof(b.Entries));
 if (!status) assert(b.Header.Ready==b.Entries[0].Ready);
 return status;
}
'''
        fixture = guard.KMD_FIXTURE.replace('@@FUNCS@@', funcs + wrapper)
        # Reuse the independently hand-constructed real page graph cases,
        # routing each through the production batch dispatcher as well.
        fixture = fixture.replace('assert(AdmissionGpuvaG3NativeQueryEscape(&a,&e)',
                                  'assert(batch_one(&a,&e)')
        guard.NativeAllocationGuard().compile_run(fixture, graph=True)

    def test_batch_wire_bounds_and_all_or_nothing_readiness(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A valid first
        allocation must not hide a later bad one, malformed envelope, missing
        entry, stale output, or failed runtime-handle authentication.
        """
        source = guard.KMD.read_text()
        self.assertTrue('AdmissionGpuvaG3NativeQueryBatchEscape(' in source)
        funcs = '\n'.join(body(source, n) for n in (
            'AdmissionG3CopyPte', 'AdmissionGpuvaG3NativeQueryEscape',
            'AdmissionGpuvaG3NativeQueryBatchEscape'))
        fixture = guard.KMD_FIXTURE.replace('@@FUNCS@@', funcs)
        start = fixture.index(' #define CHECK')
        fixture = fixture[:start] + r'''
 struct { APPLE_AGX_G3_NATIVE_QUERY_BATCH Header; APPLE_AGX_G3_NATIVE_QUERY Entries[2]; } b;
 DXGKARG_ESCAPE request=e;request.pPrivateDriverData=&b;request.PrivateDriverDataSize=sizeof(b);
 #define INIT() do {memset(&b,0,sizeof(b));b.Header.Magic=APPLE_AGX_G3_NATIVE_QUERY_BATCH_MAGIC; \
 b.Header.Version=APPLE_AGX_G3_NATIVE_QUERY_BATCH_VERSION;b.Header.Bytes=sizeof(b);b.Header.Count=2; \
 b.Entries[0]=q;b.Entries[1]=q;} while(0)
 #define RUN() AdmissionGpuvaG3NativeQueryBatchEscape(&a,&request)
 #define BAD(change) do {INIT();change;assert(RUN()!=0);assert(!b.Header.Ready && !refs && !locked);} while(0)
 INIT();assert(sizeof(b.Header)==32);assert(RUN()==0 && b.Header.Ready==1 && !refs && !locked);
 BAD(b.Header.Magic++);BAD(b.Header.Version=3);BAD(b.Header.Bytes--);
 BAD(b.Header.Count=0);BAD(b.Header.Count=1);BAD(b.Header.Count=3);
 BAD(b.Header.Count=APPLE_AGX_G3_NATIVE_QUERY_BATCH_MAX+1u);
 BAD(b.Header.Count=0xffffffffu);BAD(b.Header.FailedIndex=1);
 BAD(b.Header.Reserved[0]=1);BAD(b.Header.Reserved[1]=1);
 INIT();b.Header.Ready=1;assert(RUN()!=0);
 BAD(b.Entries[1].Ready=1);BAD(b.Entries[1].Reserved2=1);
 BAD(b.Entries[1].Allocation=43);
 BAD(request.PrivateDriverDataSize=sizeof(b)-1);request.PrivateDriverDataSize=sizeof(b);
 BAD(request.PrivateDriverDataSize=sizeof(b)+1);request.PrivateDriverDataSize=sizeof(b);
 INIT();b.Entries[1].GpuVa+=65536;
 assert(RUN()==0 && !b.Header.Ready && b.Header.FailedIndex==1);
 assert(b.Entries[0].Ready && !b.Entries[1].Ready && b.Entries[1].FailedVa);
 INIT();b.Entries[0].GpuVa+=65536;
 assert(RUN()==0 && !b.Header.Ready && !b.Header.FailedIndex);
 assert(!b.Entries[0].Ready && !b.Entries[1].Ready && !refs && !locked);
 // A resubmission must use zeroed outputs, never a previous Ready result.
 INIT();assert(RUN()==0 && b.Header.Ready);assert(RUN()!=0);
}
'''
        guard.NativeAllocationGuard().compile_run(fixture, graph=True)


if __name__ == '__main__':
    unittest.main()
