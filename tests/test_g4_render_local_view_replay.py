"""Ordinary render output is bounded by local memory, not the DCP pool."""
from pathlib import Path
import os, re, subprocess, tempfile, unittest
from test_g4_submit_virtual_replay import function_body
ROOT=Path(__file__).resolve().parents[1]
class G4RenderLocalViewReplay(unittest.TestCase):
    def test_actual_submit_accepts_high_local_output_without_widening_scanout(self):
        self._run((ROOT/'drivers/apple-agx/render-admission/src/gpuva_g3_windows.c').read_text())

    def test_successor_output_is_proven_from_its_own_packet(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? EXP2042: the
        first depth-2 kick (a render behind a running compute job of the same
        GravityMark process) failed in AdmissionGpuvaG3BeginSuccessorJob,
        because the output proof read the render packet, which still held
        the running compute job (no destination). The successful run above
        includes that state; proving the successor from the render packet
        again (the pre-fix behaviour) must break it (refusal 7)."""
        source=(ROOT/'drivers/apple-agx/render-admission/src/gpuva_g3_windows.c').read_text()
        old='AdmissionG3OutputAccessibleFor(adapter, &process->Graph, packet)'
        self.assertIn(old, source)
        with self.assertRaises(subprocess.CalledProcessError):
            self._run(source.replace(old,
                'AdmissionG3OutputAccessibleFor(adapter, &process->Graph, ((void)packet, &adapter->RenderPacket.Description))'), quiet=True)

    def _run(self, source, quiet=False):
        memory=(ROOT/'drivers/apple-agx/render-admission/src/memory_runtime_windows.c').read_text()
        branches=source.split('/* Branch IDs are a stable diagnostic ABI',1)[1]
        functions='enum {'+branches.split('enum {',1)[1].split('};',1)[0]+'};\n'
        names=('AdmissionG4ObserveEnvelopeReject','AdmissionG4SubmitRejectDetail','AdmissionG4SubmitReject',
          'AdmissionG4GraphAccess','AdmissionG4LogicalEnvelopeAccess','AdmissionG4GraphAccessTyped','AdmissionG3PresentSurfaceBytes','AdmissionG3PresentAccessible',
          'AdmissionG3PrivateSlotPreempted','AdmissionG3PrivateSlotCancelled',
          'AdmissionG4FindPrivateScene','AdmissionG4FindPrivateResubmission','AdmissionG4ResubmissionRefusal',
          'AdmissionG4PrivateGraphAccess','AdmissionG4PrivateGeometry','AdmissionG4PrivateUnqueue',
          'AdmissionG4SnapshotFailure','AdmissionG4OutputInLocal','AdmissionG4ResolveOutput',
          'AdmissionG4PreflightLocked','AdmissionGpuvaG4PreflightEscape',
          'AdmissionGpuvaG3PrivatePreempt','AdmissionGpuvaG3PrivateCancel','AdmissionG4PendingAdmissibleWhy','AdmissionG4PendingAdmissible', 'AdmissionG4RequeueAdmissible', 'AdmissionG4SubmitAwait',
          'AdmissionG4CapturePrepareLocked', 'AdmissionG4PendingQueue','AdmissionG4PendingBindHead','AdmissionG4PendingDropLocked',
          'AdmissionG4SubmitComputeLocked','AdmissionG4SubmitVirtualEnvelope',
          'AdmissionG3OutputMatchesLocalFor','AdmissionG3OutputAccessibleFor','AdmissionGpuvaG3BeginJobAs',
          'AdmissionGpuvaG3BeginJob','AdmissionGpuvaG3BeginSuccessorJob','AdmissionGpuvaG3AbortJob')
        functions+='\n'.join(function_body(source,n) for n in names)
        shim=(ROOT/'tests/g4_submit_virtual_replay.c').read_text()
        prefix=shim.split('int main(void) {',1)[0]
        old=function_body(prefix,'AdmissionMemoryRuntimeScanoutView')
        prefix=prefix.replace(function_body(prefix,'AdmissionMemoryRuntimeLocalView'),'')
        local="""static NTSTATUS AdmissionMemoryRuntimeLocalView(ADMISSION_CONTEXT *adapter,
            ADMISSION_SCANOUT_MEMORY_VIEW *view) {
            (void)adapter; if(!NT_SUCCESS(scanout_status))return scanout_status;
            static unsigned char memory[0x4050000];
            view->CpuAddress=memory;view->GuestIpaAddress=0x8e0000000ULL;
            view->HostPhysicalAddress=0x8e0000000ULL;view->Bytes=952ULL*1024*1024;
            view->PoolBytes=0;return STATUS_SUCCESS;
        }"""
        prefix=prefix.replace(old,local+'\n'+function_body(memory,'AdmissionMemoryRuntimeScanoutView'))
        prefix=prefix.replace('HostPhysicalAddress, Bytes;', 'HostPhysicalAddress, Bytes, PoolBytes;')
        # rc102 (EXP2045): AdmissionGpuvaG3AbortJob wakes JOB_BEGIN waiters.
        prefix=prefix.replace('  unsigned SuccessorFence;\n', '  unsigned SuccessorFence;\n  int JobEvent;\n')
        prefix=prefix.replace('static ULONGLONG output_ipa=0x90001000ULL;',
            'static ULONGLONG output_ipa=0x8e4040000ULL;')
        prefix=prefix.replace('static int output_mapped=1;', 'static int output_mapped=1, output_split=0; static ULONGLONG output_va=0x40000000ULL, output_bytes=0x8000ULL;')
        prefix=prefix.replace('va<0x40000000ULL || va>=0x40001000ULL', 'va<output_va || va-output_va>=output_bytes').replace('*ipa=output_ipa+(va-0x40000000ULL);', '*ipa=output_ipa+(va-output_va)+(output_split && va>=output_va+0x4000ULL ? 0x1000ULL : 0ULL);')
        prefix=('#define APPLE_AGX_EXP907_FRAME_RECEIPT 1\n'
                '#define APPLE_AGX_SCANOUT_J313_SURFACE_SIZE 0xfa0000ULL\n'
                '#define APPLE_AGX_SCANOUT_J313_POOL_MAX_SIZE 0x3b800000ULL\n'
                '#define APPLE_AGX_SCANOUT_ALIGNMENT 0x10000ULL\n'+prefix)
        prefix=prefix.replace('#include "g4_submit_virtual_functions.inc"',
            '\n#include "render_qualification.h"\n'
            'static ADMISSION_DWM_ENVELOPE_RECEIPT observed_receipt;\n'
            'static void AdmissionDwmFrameRecordReject(ADMISSION_CONTEXT *a,void *c,ULONG b,NTSTATUS s){(void)a;(void)c;(void)b;(void)s;}\n'
            'static unsigned long long KeQueryInterruptTime(void){return 1234;}\n'
            'static void AdmissionDwmFrameRecordEnvelope(ADMISSION_CONTEXT *a,void *c,const ADMISSION_DWM_ENVELOPE_RECEIPT *r){(void)a;(void)c;observed_receipt=*r;}\n'
            '#ifndef IO_NO_INCREMENT\n#define IO_NO_INCREMENT 0\n#endif\n'
            'static int graph_end_calls;\n'
            'static int AppleAgxGpuvaG3GraphEndJob(APPLE_AGX_GPUVA_G3_GRAPH *g){(void)g;++graph_end_calls;return 1;}\n'
            'static int AppleAgxGpuvaG3GraphReleaseRetained(APPLE_AGX_GPUVA_G3_GRAPH *g){(void)g;return 1;}\n'
            'static void KeSetEvent(int *e,int i,int w){(void)i;(void)w;*e=1;}\n'
            '#include "g4_submit_virtual_functions.inc"')
        setup=shim.split('int main(void) {',1)[1].split('  assert(AdmissionG4SubmitVirtualEnvelope',1)[0]
        ending="""
          AdmissionG4ObserveEnvelopeReject(&adapter,&context,&args,1,13,8,NULL,NULL);
          assert(observed_receipt.Stage==1 && observed_receipt.InterruptTime==1234);
          assert(observed_receipt.Predicate==13 && observed_receipt.RuntimePredicate==8);
          ADMISSION_G3_PRIVATE_SCENE diagnostic_scene={0};
          APPLE_AGX_G4_PRIVATE_LEASE diagnostic_lease={0};
          diagnostic_lease.ManagerId=process.Graph.ProcessId;
          diagnostic_lease.ManagerGeneration=7;
          diagnostic_lease.SceneId=diagnostic_lease.SceneGeneration=9;
          diagnostic_scene.Context=&context;diagnostic_scene.Storage.Generation=9;
          diagnostic_scene.Queued=1;diagnostic_scene.Started=1;diagnostic_scene.Fence=21;
          process.PrivateScenes=&diagnostic_scene;
          AdmissionG4ObserveEnvelopeReject(&adapter,&context,&args,2,0,0,&process,&diagnostic_lease);
          assert(observed_receipt.SceneState==41 && observed_receipt.SceneFence==21);
          assert(observed_receipt.LeaseManagerGeneration==7 && observed_receipt.LeaseSceneId==9);
          assert(diagnostic_scene.Queued==1 && diagnostic_scene.Started==1 && diagnostic_scene.Fence==21);
          process.PrivateScenes=NULL;
          adapter.RuntimeReady=0;
          assert(AdmissionG4SubmitVirtualEnvelope(&adapter,&context,&args)==STATUS_INVALID_PARAMETER);
          assert(observed_receipt.Predicate==13 && observed_receipt.RuntimePredicate==4);
          adapter.RuntimeReady=1;
          context.GpuvaG3Poisoned=1;
          assert(AdmissionG4SubmitVirtualEnvelope(&adapter,&context,&args)==STATUS_INVALID_PARAMETER);
          assert(observed_receipt.Predicate==6 && observed_receipt.RuntimePredicate==0);
          context.GpuvaG3Poisoned=0;
          adapter.G4SubmitFailureClaim=0;
          memset(&adapter.G4SubmitFailure,0,sizeof(adapter.G4SubmitFailure));
          NTSTATUS status=AdmissionG4SubmitVirtualEnvelope(&adapter,&context,&args);
          fprintf(stderr,"submit=%08x branch=%u subsite=%u\\n",(unsigned)status,
            adapter.G4SubmitFailure.Branch,adapter.G4SubmitFailure.Subsite);
          assert(status==STATUS_SUCCESS);
          assert(dispatches==1 && adapter.RenderPacket.State==AdmissionRenderPacketQueued);
          assert(adapter.RenderPacket.Description.DestinationPhysical==output_ipa);
          adapter.RenderPacket.State=3; /* Actual worker activation precedes BeginJob. */
          NTSTATUS begin=AdmissionGpuvaG3BeginJob(&adapter,&context,args.SubmissionFenceId);
          fprintf(stderr,"begin=%08x GPU_JOB_BEGIN_calls=%d\\n",(unsigned)begin,graph_begin_calls);
          assert(begin==STATUS_SUCCESS && graph_begin_calls==1);
          assert(state.ActiveProcess==&process && state.ActiveFence==args.SubmissionFenceId);
          {
            /* rc102 (EXP2042): the same render as a depth-2 successor behind a
             * running compute job of its process: the render packet holds the
             * compute job (no destination), the render is the Kicked head. */
            ADMISSION_RENDER_PACKET_DESCRIPTION own=adapter.RenderPacket.Description;
            ADMISSION_RENDER_PACKET_DESCRIPTION running;
            ULONG fence=args.SubmissionFenceId, refusal=99u;
            memset(&running,0,sizeof(running));
            running.Fence=fence-1u; running.Kind=ADMISSION_RENDER_PACKET_KIND_COMPUTE;
            adapter.RenderPacket.Description=running;
            adapter.G4PendingHead=0u; adapter.G4PendingCount=1u;
            adapter.G4Pending[0].State=AdmissionG4PendingKicked;
            adapter.G4Pending[0].Fence=fence; adapter.G4Pending[0].Context=&context;
            adapter.G4Pending[0].Packet=own;
            state.ActiveFence=fence-1u; state.SuccessorFence=0u; state.SuccessorContext=NULL;
            NTSTATUS next=AdmissionGpuvaG3BeginSuccessorJob(&adapter,&context,fence,&refusal);
            fprintf(stderr,"successor=%08x refusal=%u nested=%d\\n",(unsigned)next,refusal,graph_nested_calls);
            assert(next==STATUS_SUCCESS && refusal==0u && graph_nested_calls==1);
            assert(state.SuccessorFence==fence && state.SuccessorContext==&context &&
                   state.ActiveFence==fence-1u && state.ActiveProcess==&process);
            {
              /* EXP2045: SubmitSuccessor then refused, nothing reached the
               * firmware: the kick ends the successor's G3 job unrun (its
               * nested lease, its started scene); the running job keeps
               * the lease and its own state. */
              ADMISSION_G3_PRIVATE_SCENE refused, *scenes=process.PrivateScenes;
              memset(&refused,0,sizeof(refused));
              refused.Context=&context; refused.Queued=1u; refused.Started=1u;
              refused.Fence=fence; refused.Next=scenes; process.PrivateScenes=&refused;
              AdmissionGpuvaG3AbortJob(&adapter,fence);
              assert(graph_end_calls==1 && state.SuccessorFence==0u &&
                     state.SuccessorContext==NULL);
              assert(state.ActiveFence==fence-1u && state.ActiveProcess==&process);
              assert(!refused.Started && refused.Queued && state.JobEvent);
              process.PrivateScenes=scenes;
            }
            /* no kicked entry for that fence: refused before any check */
            state.SuccessorFence=0u; adapter.G4Pending[0].State=AdmissionG4PendingQueued;
            assert(AdmissionGpuvaG3BeginSuccessorJob(&adapter,&context,fence,&refusal)!=STATUS_SUCCESS &&
                   refusal==9u && graph_nested_calls==1);
            /* another process's active job: refusal 1 */
            adapter.G4Pending[0].State=AdmissionG4PendingKicked; state.ActiveProcess=NULL;
            assert(AdmissionGpuvaG3BeginSuccessorJob(&adapter,&context,fence,&refusal)!=STATUS_SUCCESS &&
                   refusal==1u && graph_nested_calls==1);
            adapter.G4PendingCount=0u; adapter.G4Pending[0].State=AdmissionG4PendingFree;
            adapter.RenderPacket.Description=own;
          }
          state.ActiveProcess=NULL;state.ActiveFence=0;
          output_ipa+=0x4000ULL;
          assert(AdmissionGpuvaG3BeginJob(&adapter,&context,args.SubmissionFenceId)!=STATUS_SUCCESS);
          assert(graph_begin_calls==1);output_ipa-=0x4000ULL;
          adapter.BackendImage.G4Native=0;
          /* M2-f2c: the DCP pool is all of segment 2 (EXP953), so the
           * scanout view the non-native path checks is the local view. */
          assert(AdmissionG3OutputMatchesLocalFor(&adapter,&process.Graph,&adapter.RenderPacket.Description));
          adapter.BackendImage.G4Native=1;
          ADMISSION_SCANOUT_MEMORY_VIEW scanout={0};
          assert(AdmissionMemoryRuntimeScanoutView(&adapter,&scanout)==STATUS_SUCCESS);
          /* M2-f2c: the scanout view is the whole receipt-sized segment 2. */
          assert(scanout.Bytes==952ULL*1024*1024 && scanout.PoolBytes==scanout.Bytes);
          /* A 56 MiB pool (a smaller reserve) excludes the 64 MiB output. */
          scanout.Bytes=scanout.PoolBytes=0x3800000ULL;
          ADMISSION_RENDER_PACKET_DESCRIPTION out={0};
          /* Outside the DCP pool the output is VA-only (M2-c), never a scanout
           * physical address. */
          assert(AdmissionG4ResolveOutput(&process,&packet.Attachment,&scanout,&out) &&
                 out.DestinationPhysical==0 && out.DestinationCpuToken==0);
          ADMISSION_SCANOUT_MEMORY_VIEW local={0};
          assert(AdmissionMemoryRuntimeLocalView(&adapter,&local)==STATUS_SUCCESS);
          packet.Attachment.Size=0x8000u;
          assert(AdmissionG4ResolveOutput(&process,&packet.Attachment,&local,&out));
          output_split=1;
          assert(!AdmissionG4ResolveOutput(&process,&packet.Attachment,&local,&out));
          output_split=0;
          output_ipa=local.GuestIpaAddress+local.Bytes;
          assert(!AdmissionG4ResolveOutput(&process,&packet.Attachment,&local,&out));
          output_ipa=local.GuestIpaAddress-0x1000ULL;
          assert(!AdmissionG4ResolveOutput(&process,&packet.Attachment,&local,&out));
          /* M2-c: outside the local reserve but mapped writable: VA only. */
          output_va_only=packet.Attachment.Pointer;output_va_only_bytes=(unsigned)packet.Attachment.Size;
          assert(AdmissionG4ResolveOutput(&process,&packet.Attachment,&local,&out));
          assert(out.DestinationGpuVa==packet.Attachment.Pointer && out.DestinationPhysical==0 &&
                 out.DestinationCpuToken==0 && out.DestinationBytes==packet.Attachment.Size);
          output_va_only_bytes=0;
          output_mapped=0;
          assert(!AdmissionG4ResolveOutput(&process,&packet.Attachment,&local,&out));
          output_mapped=1;output_va=0x860000ULL;output_bytes=0xfd2000ULL;output_ipa=0x8e2d10000ULL;
          adapter.RenderPacket.Description.DestinationGpuVa=output_va;
          adapter.RenderPacket.Description.DestinationPhysical=output_ipa;
          adapter.RenderPacket.Description.DestinationBytes=(ULONG)output_bytes;
          assert(AdmissionG3OutputMatchesLocalFor(&adapter,&process.Graph,&adapter.RenderPacket.Description));
          adapter.BackendImage.G4Native=0;
          /* M2-f2c: the DCP pool is all of segment 2 (EXP953), so the
           * scanout view the non-native path checks is the local view. */
          assert(AdmissionG3OutputMatchesLocalFor(&adapter,&process.Graph,&adapter.RenderPacket.Description));
          /* M2-c: a VA-only destination at BeginJob: mapped writable or not. */
          adapter.BackendImage.G4Native=1;
          adapter.RenderPacket.Description.DestinationPhysical=0;
          adapter.RenderPacket.Description.DestinationCpuToken=0;
          output_va_only=output_va;output_va_only_bytes=(unsigned)output_bytes;
          assert(AdmissionG3OutputAccessibleFor(&adapter,&process.Graph,&adapter.RenderPacket.Description));
          output_va_only_bytes=0;
          assert(!AdmissionG3OutputAccessibleFor(&adapter,&process.Graph,&adapter.RenderPacket.Description));
          puts("high local render admitted through BeginJob; exact935 span and legacy scanout guards checked");return 0;
        }"""
        with tempfile.TemporaryDirectory(prefix='g4-render-local-') as d:
            d=Path(d);(d/'g4_submit_virtual_functions.inc').write_text(functions)
            (d/'replay.c').write_text(prefix+'int main(void) {'+setup+ending)
            exe=d/'replay';subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
              '-Wno-unused-function','-fsanitize=address,undefined','-I',str(d),'-I',str(ROOT/'drivers/apple-agx/shared/include'),'-I',str(ROOT/'drivers/apple-agx/render-admission/include'),
              str(d/'replay.c'),str(ROOT/'drivers/apple-agx/shared/src/apple_agx_g4_submit.c'),
              str(ROOT/'drivers/apple-agx/shared/src/apple_agx_scheduler.c'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,timeout=15,
                           stderr=subprocess.DEVNULL if quiet else None)
