"""Ordinary render output is bounded by local memory, not the DCP pool."""
from pathlib import Path
import os, re, subprocess, tempfile, unittest
from test_g4_submit_virtual_replay import function_body
ROOT=Path(__file__).resolve().parents[1]
class G4RenderLocalViewReplay(unittest.TestCase):
    def test_actual_submit_accepts_high_local_output_without_widening_scanout(self):
        source=(ROOT/'drivers/apple-agx/render-admission/src/gpuva_g3_windows.c').read_text()
        memory=(ROOT/'drivers/apple-agx/render-admission/src/memory_runtime_windows.c').read_text()
        branches=source.split('/* Branch IDs are a stable diagnostic ABI',1)[1]
        functions='enum {'+branches.split('enum {',1)[1].split('};',1)[0]+'};\n'
        names=('AdmissionG4SubmitRejectDetail','AdmissionG4SubmitReject',
          'AdmissionG4GraphAccess','AdmissionG4LogicalEnvelopeAccess','AdmissionG4GraphAccessTyped',
          'AdmissionG4FindPrivateScene','AdmissionG4FindPrivateResubmission',
          'AdmissionG4PrivateGraphAccess','AdmissionG4PrivateGeometry','AdmissionG4PrivateUnqueue',
          'AdmissionG4SnapshotFailure','AdmissionG4ResolveOutput','AdmissionG4SubmitVirtualEnvelope',
          'AdmissionG3OutputMatchesLocal','AdmissionGpuvaG3BeginJob')
        functions+='\n'.join(function_body(source,n) for n in names)
        shim=(ROOT/'tests/g4_submit_virtual_replay.c').read_text()
        prefix=shim.split('int main(void) {',1)[0]
        old=function_body(prefix,'AdmissionMemoryRuntimeScanoutView')
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
        prefix=prefix.replace('static ULONGLONG output_ipa=0x90001000ULL;',
            'static ULONGLONG output_ipa=0x8e4040000ULL;')
        prefix=prefix.replace('static int output_mapped=1;', 'static int output_mapped=1, output_split=0; static ULONGLONG output_va=0x40000000ULL, output_bytes=0x8000ULL;')
        prefix=prefix.replace('va<0x40000000ULL || va>=0x40001000ULL', 'va<output_va || va-output_va>=output_bytes').replace('*ipa=output_ipa+(va-0x40000000ULL);', '*ipa=output_ipa+(va-output_va)+(output_split && va>=output_va+0x4000ULL ? 0x1000ULL : 0ULL);')
        prefix='#define APPLE_AGX_SCANOUT_J313_POOL_SIZE 0x3800000ULL\n'+prefix
        setup=shim.split('int main(void) {',1)[1].split('  assert(AdmissionG4SubmitVirtualEnvelope',1)[0]
        ending="""
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
          state.ActiveProcess=NULL;state.ActiveFence=0;
          output_ipa+=0x4000ULL;
          assert(AdmissionGpuvaG3BeginJob(&adapter,&context,args.SubmissionFenceId)!=STATUS_SUCCESS);
          assert(graph_begin_calls==1);output_ipa-=0x4000ULL;
          adapter.BackendImage.G4Native=0;
          assert(!AdmissionG3OutputMatchesLocal(&adapter,&process.Graph));
          adapter.BackendImage.G4Native=1;
          ADMISSION_SCANOUT_MEMORY_VIEW scanout={0};
          assert(AdmissionMemoryRuntimeScanoutView(&adapter,&scanout)==STATUS_SUCCESS);
          assert(scanout.Bytes==0x3800000ULL && scanout.PoolBytes==scanout.Bytes);
          ADMISSION_RENDER_PACKET_DESCRIPTION out={0};
          assert(!AdmissionG4ResolveOutput(&process,&packet.Attachment,&scanout,&out));
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
          output_mapped=0;
          assert(!AdmissionG4ResolveOutput(&process,&packet.Attachment,&local,&out));
          output_mapped=1;output_va=0x860000ULL;output_bytes=0xfd2000ULL;output_ipa=0x8e2d10000ULL;
          adapter.RenderPacket.Description.DestinationGpuVa=output_va;
          adapter.RenderPacket.Description.DestinationPhysical=output_ipa;
          adapter.RenderPacket.Description.DestinationBytes=(ULONG)output_bytes;
          assert(AdmissionG3OutputMatchesLocal(&adapter,&process.Graph));
          adapter.BackendImage.G4Native=0;
          assert(!AdmissionG3OutputMatchesLocal(&adapter,&process.Graph));
          puts("high local render admitted through BeginJob; exact935 span and legacy scanout guards checked");return 0;
        }"""
        with tempfile.TemporaryDirectory(prefix='g4-render-local-') as d:
            d=Path(d);(d/'g4_submit_virtual_functions.inc').write_text(functions)
            (d/'replay.c').write_text(prefix+'int main(void) {'+setup+ending)
            exe=d/'replay';subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
              '-Wno-unused-function','-fsanitize=address,undefined','-I',str(d),'-I',str(ROOT/'drivers/apple-agx/shared/include'),
              str(d/'replay.c'),str(ROOT/'drivers/apple-agx/shared/src/apple_agx_g4_submit.c'),
              str(ROOT/'drivers/apple-agx/shared/src/apple_agx_scheduler.c'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,timeout=15)
