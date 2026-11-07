"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

Buffer copies must reach the resource-scoped copy helper while unrelated GPU
work is pending. A redundant whole-context retire used to block both CopyResource
and CopySubresourceRegion. Execute their actual projected dispatch blocks, with
Windows callbacks reporting a busy context, and check offsets/error propagation.
The real helper's source-writer/destination-user waits are intentionally retained.
"""
import unittest

import test_d3d10_resource_sync_scope as scope


class CopySyncScope(unittest.TestCase):
    run_cpp = scope.ResourceSyncScope.run_cpp

    def test_buffer_copies_do_not_drain_unrelated_work(self):
        whole = scope.projected_body('ResourceCopy')
        whole = whole[whole.index('      HRESULT result=AgxD3d10WindowsFlushStatus'):]
        whole = whole[:whole.index('      return;')]
        region = scope.projected_body('ResourceCopyRegion')
        region = region[region.index('      HRESULT result=AgxD3d10WindowsFlushStatus'):]
        region = region[:region.index('      return;')]
        self.run_cpp(r'''
struct Buffer { unsigned width0; };
static unsigned copies, got_dst, got_src, got_bytes;
static bool fail_copy;
static int AgxWin32AsahiBufferCopy(pipe_context *, Buffer *, unsigned dst,
                                 Buffer *, unsigned src, unsigned bytes) {
 ++copies; got_dst=dst; got_src=src; got_bytes=bytes; return !fail_copy;
}
static void whole_copy(Device *device, Buffer *dst, Buffer *src) {
 void *hDevice=device;
WHOLE
}
static void region_copy(Device *device, Buffer *dst, Buffer *src,
                        UINT DstX, UINT left, UINT right) {
 void *hDevice=device;
REGION
}
int main() {
 pipe_context pipe; Device device={nullptr,&pipe}; Buffer dst={128},src={128};
 whole_copy(&device,&dst,&src);
 assert(copies==1 && !drains && !error && !got_dst && !got_src && got_bytes==128);
 region_copy(&device,&dst,&src,31,7,23);
 assert(copies==2 && !drains && !error && got_dst==31 && got_src==7 && got_bytes==16);
 region_copy(&device,&dst,&src,31,7,7);
 assert(copies==2 && !drains && !error);
 fail_copy=true;
 whole_copy(&device,&dst,&src); assert(copies==3 && error==E_FAIL);
 error=0;
 region_copy(&device,&dst,&src,0,0,16); assert(copies==4 && error==E_FAIL);
 // Sticky backend errors must stop both operations before copying.
 error=-42; fail_copy=false;
 whole_copy(&device,&dst,&src);
 region_copy(&device,&dst,&src,0,0,16);
 assert(copies==4 && error==-42 && !drains);
}
'''.replace('WHOLE', whole).replace('REGION', region))


if __name__ == '__main__':
    unittest.main()
