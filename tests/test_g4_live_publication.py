"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Execute real KMD BuildPagingBuffer, graph/client and negotiated broker: a fresh
map must publish with the head alive, while destructive updates still wait.
The older all-DDI replay has unrelated shim drift; this focused fixture uses
its OS/memory model and only the production paging closure needed here.
"""
from pathlib import Path
import os, re, subprocess, tempfile, unittest, shutil
import g3_vidmm_replay as replay
ROOT=replay.ROOT
SRC=replay.SRC
SHARED=replay.SHARED
M1N1=Path(os.environ.get('Q22_M1N1_SOURCE', str(ROOT/'m1n1_windows')))/'src'

class LivePublication(unittest.TestCase):
    def test_actual_build_paging_with_live_broker(self):
        with tempfile.TemporaryDirectory(prefix='q22-live-ddi-') as d:
            d=Path(d)
            broker_src=d/"m1n1/src"
            shutil.copytree(M1N1,broker_src)
            (d/"drivers/apple-agx").mkdir(parents=True)
            (d/"drivers/apple-agx/shared").symlink_to(SHARED.resolve())
            shim=(ROOT/'tests/g3_vidmm_replay_shim.h').read_text()
            shim=shim.replace('"../drivers/', '"'+str(ROOT)+'/drivers/')
            shim=shim.replace('  ULONG CpuQueueCount,PagingFence,','  ULONG LiveAdditivePaging, PagingActiveNode, PagingRecordCount; ADMISSION_PAGING_RECORD PagingRecords[64];\n  ULONG CpuQueueCount,PagingFence,')
            shim=shim.replace('typedef struct _ADMISSION_G3_STATE {', '#include \"render_omitted_leaves.h\"\ntypedef struct _ADMISSION_G3_STATE { ADMISSION_OMITTED_LEAVES OmittedLeaves;')
            shim=shim.replace('static bool ReplayInvalidate(void *opaque,unsigned slot) {', 'static unsigned live_invalidate_count, live_fail_invalidation;\nstatic bool ReplayInvalidate(void *opaque,unsigned slot) {if(live_fail_invalidation && ++live_invalidate_count==live_fail_invalidation)return false;')
            shim += '\nstatic void AdmissionRecordG4OmittedLeaves(ADMISSION_CONTEXT *a,const ADMISSION_OMITTED_LEAVES *r) {(void)a;(void)r;}\n'
            shim=shim.replace('#define PASSIVE_LEVEL 0','#define APC_LEVEL 1\n#define PASSIVE_LEVEL 0')
            (d/'g3_vidmm_replay_shim.h').write_text(shim)
            parts=['#include "g3_vidmm_replay_shim.h"\n#include "apple_agx_gpuva_g3_caps.h"\nstatic bool gpuva_capacity_reported;\nstatic void AdmissionRecordGpuvaG3BrokerFailures(void *a,void *b) {(void)a;(void)b;}\n']
            private=(SRC/'gpuva_g3_private.h').read_text().replace('static inline ', 'static ')
            for name in ('AdmissionG3NativeInvalidatePtes','AdmissionG3AttachPrivateLeaves','AdmissionG3PrivateLeafSet'):
                parts.append(replay.body(private,name))
            windows=(SRC/'gpuva_g3_windows.c').read_text()
            for name in ('AdmissionG3AllocateNode','AdmissionG3FreeNode','AdmissionGpuvaG3FindProcess',
                         'AdmissionG3LockFoundProcess','AdmissionG3FindLockedProcess',
                         'AdmissionGpuvaG3BrokerTable','AdmissionGpuvaG3MirrorTable','AdmissionGpuvaG3ResolveTable'):
                parts.append(replay.body(windows,name))
            paging=(SRC/'gpuva_g3_paging_windows.c').read_text()
            if os.environ.get('Q22_NEGATIVE')=='retention':
                paging=paging.replace('shadow->PendingPtes = candidate;', '/* negative control: lost quarantine ownership */')
            if os.environ.get('Q22_NEGATIVE')=='wait':
                paging=paging.replace('live_update || timed_out)', 'timed_out)')
            parts.append(paging[paging.index('enum {'):paging.index('static NTSTATUS AdmissionG3RejectPaging')])
            names=re.findall(r'(?m)^(?:static )?(?:NTSTATUS|BOOLEAN|VOID|void|ULONG|UINT|ULONGLONG|APPLE_AGX_GPUVA_G3_NODE \*)\s*(Admission\w+)\s*\(',paging)
            for name in names:
                if name in ('AdmissionGpuvaG3BuildPagingBufferChecked','AdmissionG3PagingStatusAllowed') or name.startswith('AdmissionG3NativeStaging'): continue
                parts.append(replay.body(paging,name))
            parts.append(replay.body((SRC/'paging_windows.c').read_text(),'AdmissionPagingAcquireMutation'))
            platform=(M1N1/'hv_agx_retained_platform.c').read_text()
            parts.extend(replay.body(platform,n) for n in ('translate_guest','gpuva_execute'))
            parts.append((ROOT/'tests/g4_live_publication_case.c').read_text())
            (d/'case.c').write_text('\n'.join(parts))
            sources=[SHARED/'src'/n for n in ('apple_agx_gpuva_g3_translation.c',
                'apple_agx_gpuva_g3_graph.c','apple_agx_gpuva_broker_v5_client.c')]
            sources += [SRC/'render_allocation.c',SRC/'render_pool_demand.c',SRC/'render_paging.c']
            sources += [broker_src/n for n in ('hv_agx_gpuva_v5.c','hv_agx_gpuva_v5_mmio.c',
                'hv_agx_retained_backing.c','hv_agx_retained_tables.c')]
            cmd=['clang','-std=gnu11','-O0','-g','-Wno-unused-function','-Wno-multichar',
                '-ftrivial-auto-var-init=pattern','-fsanitize=address,undefined',
                '-I',str(d),'-I',str(ROOT/'tests'),'-I',str(broker_src),
                '-I',str(SHARED/'include'),'-idirafter',str(SRC.parent/'include'),
                str(d/'case.c'),*map(str,sources),'-o',str(d/'case')]
            r=subprocess.run(cmd,text=True,capture_output=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            r=subprocess.run([str(d/'case')],text=True,capture_output=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            self.assertIn('live KMD publication PASS',r.stdout)
