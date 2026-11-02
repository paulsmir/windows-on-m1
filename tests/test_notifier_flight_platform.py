"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The platform must reserve before external construction and wire the notifier
transaction into the queue runtime; failed external construction must cancel.
This executes the real platform callbacks with the existing transport fixture.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_render_job_depth2_caps_rc98 import SHARED

class FlightPlatform(unittest.TestCase):
    def test_opt_in_callback_lifecycle(self):
        source=(SHARED/'tests/apple_agx_platform_provider_test.c').read_text()
        source=source.replace('  unsigned int ExternalBuildCalls;', '  APPLE_AGX_G13_QUEUE_RUNTIME_IO RuntimeIo;\n  unsigned int FailBuild;\n  unsigned int ExternalBuildCalls;')
        source=source.replace('  g_components->CapturedProviderIo = *ProviderIo;', '  g_components->CapturedProviderIo = *ProviderIo;\n  g_components->RuntimeIo=*RuntimeIo;')
        source=source.replace('  ++g_components->ExternalBuildCalls;', '  ++g_components->ExternalBuildCalls;\n  if(g_components->FailBuild) return 0;')
        start=source.index('static void test_external_rebased_image_mode_uses_exact_job_and_ranges(')
        end=source.index('\nstatic void ',start+1)
        body=source[start:end]
        body=body.replace('  config.ChannelMemory = &owner;', '  config.ChannelMemory = &owner;\n  config.RenderSharedMemory->NotifierFlight=1;')
        body=body.replace('  assert(components.ExternalBuildCalls == 1u);', '''
  assert(components.ExternalBuildCalls == 1u);
  assert(config.RenderSharedMemory->BuildingFlight==1);
  assert(components.RuntimeIo.PreparePublish && components.RuntimeIo.FinishPublish);
  config.RenderSharedMemory->Flights[0].EventCount=5;
  assert(components.RuntimeIo.PreparePublish(components.RuntimeIo.Context,77));
  assert(*(unsigned *)config.RenderSharedMemory->Objects[12].CpuAddress==5);
  assert(components.RuntimeIo.FinishPublish(components.RuntimeIo.Context,77,0));
  assert(!config.RenderSharedMemory->BuildingFlight);
  components.FailBuild=1;
  assert(!components.CapturedRenderIo.Image.Relocate(
      components.CapturedRenderIo.Context,external.Ranges,sizeof(external.Ranges),
      &roots,command,sizeof(command),&submission,&job));
  assert(!config.RenderSharedMemory->BuildingFlight && !config.RenderSharedMemory->Flights[0].Fence);
  components.FailBuild=0;
''')
        source=source[:start]+body+source[end:]
        names=['apple_agx_event_allocator.c','apple_agx_platform_provider.c',
            'apple_agx_render_shared_memory.c','apple_agx_g13_compute_work.c',
            'apple_agx_relocation.c','apple_agx_render_template.generated.c','apple_agx_memory.c']
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'test.c';c.write_text(source);b=Path(d)/'test'
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-ftrivial-auto-var-init=pattern',
                '-I',str(SHARED/'include'),str(c),*[str(SHARED/'src'/n) for n in names],
                '-o',str(b)],check=True)
            subprocess.run([str(b)],check=True)
