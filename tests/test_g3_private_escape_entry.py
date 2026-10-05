"""Real KMD/broker private scene lifecycle without adapter-global idle entry."""
import importlib.util
import os
from pathlib import Path
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
class PrivateEscapeEntryTests(unittest.TestCase):
 def test_software_entry_preserves_real_private_scene_lifecycle(self):
  spec=importlib.util.spec_from_file_location('private_entry_replay',ROOT/'tests/g3_vidmm_replay.py')
  m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
  original=m.generate
  def generate(*args,**kwargs):
   source=original(*args,**kwargs)
   scenarios=(ROOT/'tests/g3_vidmm_replay_scenarios.c').read_text()
   private=(ROOT/'tests/g3_r137_private_combined.c').read_text()
   marker='  t->Escape.pPrivateDriverData=q;t->Escape.PrivateDriverDataSize=sizeof(*q);'
   assert private.count(marker)==1
   private=private.replace(marker,marker+r'''
  t->Escape.Flags.Value=2u;
  assert(AdmissionDdiEscape(t->Adapter,&t->Escape)==STATUS_INVALID_PARAMETER);
  t->Escape.Flags.Value=0u;
''')
   checkpoint='  assert(prepare_process_buffers(&umd,&batch,r,ranges));'
   assert private.count(checkpoint)==1
   private=private.replace(checkpoint,checkpoint+r'''
  /* A retained completion lease must block preparation before reaping a
   * release-requested scene, even after the wait reaches its bound. */
  ADMISSION_G3_PRIVATE_SCENE *held=p->PrivateScenes;
  ULONGLONG held_generation=held->Storage.Generation;
  held->ReleaseRequested=1u;p->Graph.LeaseToken=1u;
  AGX_G4_BATCH blocked={0};APPLE_AGX_G4_PROCESS_RANGE blocked_ranges[9];
  assert(!prepare_process_buffers(&umd,&blocked,r,blocked_ranges));
  assert(a.G3PrivateFailure.Status==(UINT)STATUS_DEVICE_BUSY);
  assert(a.G3PrivateFailure.Branch==7u);
  assert(p->PrivateScenes==held && held->Storage.Generation==held_generation);
  assert(held->ReleaseRequested==1u && !blocked.Lease.SceneId);
  p->Graph.LeaseToken=0;held->ReleaseRequested=0;
  a.G3PrivateFailureClaim=0;
''')
   # A second, unused scene must remain mapped while another real owner job
   # runs. Release acknowledges ownership transfer, not premature reclamation.
   marker='  struct agx_resource target={.bo=&target'
   assert private.count(marker)==1
   private=private.replace(marker,r'''
  AGX_G4_BATCH deferred_batch={0};
  APPLE_AGX_G4_PROCESS_RANGE deferred_ranges[9];
  assert(prepare_process_buffers(&umd,&deferred_batch,r,deferred_ranges));
  ADMISSION_G3_PRIVATE_SCENE *deferred_scene=p->PrivateScenes;
  assert(deferred_scene && deferred_scene->Storage.Generation==deferred_batch.Lease.SceneId);
  unsigned deferred_offset=deferred_scene->Storage.Extents[0].Offset;
  unsigned deferred_bytes=deferred_scene->Storage.Extents[0].Bytes;
  unsigned char *deferred_cpu=local_cpu+(40u<<20)+deferred_offset;
  memset(deferred_cpu,0xa6,deferred_bytes);
  APPLE_AGX_G3_PRIVATE_REQUEST deferred_release={0};
  deferred_release.Magic=APPLE_AGX_G3_PRIVATE_MAGIC;
  deferred_release.Version=1;deferred_release.Bytes=sizeof(deferred_release);
  deferred_release.Operation=APPLE_AGX_G3_PRIVATE_RELEASE;
  deferred_release.ManagerId=deferred_batch.Lease.ManagerId;
  deferred_release.ManagerGeneration=deferred_batch.Lease.ManagerGeneration;
  deferred_release.SceneId=deferred_batch.Lease.SceneId;
  deferred_release.SceneGeneration=deferred_batch.Lease.SceneGeneration;
''' +marker)
   marker='  assert(scene->Started && p->Graph.JobInFlight);'
   assert private.count(marker)==1
   private=private.replace(marker,marker+r'''
  assert(r137_escape_transport(&t,&deferred_release));
  assert(deferred_scene->ReleaseRequested && !deferred_scene->Queued);
  assert(state.PrivatePool.Blocks[deferred_offset>>16].Owner);
  assert(deferred_cpu[0]==0xa6);
  assert(AdmissionG3PrivateReap(p));
  assert(deferred_cpu[0]==0xa6 && state.PrivatePool.Blocks[deferred_offset>>16].Owner);
  { ULONGLONG mapped=0;
    assert(AppleAgxGpuvaG3GraphTranslateVa(&p->Graph,deferred_ranges[3].Va,&mapped));
    assert(mapped==local_ipa+vidmm_local_bytes+deferred_offset);
  }
''')
   marker='\n  assert(!p->PrivateScenes && !context.GpuvaG3PrivateFence);'
   assert private.count(marker)==1
   private=private.replace(marker,marker+r'''
  assert(!state.PrivatePool.Blocks[deferred_offset>>16].Owner);
  for(unsigned i=0;i<deferred_bytes;++i)assert(deferred_cpu[i]==0);
''')
   scenarios=scenarios.replace('#include "g3_r137_private_combined.c"',private)
   return source.replace('#include "g3_vidmm_replay_scenarios.c"',scenarios)
  m.generate=generate
  for profile in ('16','64'):
   with self.subTest(profile=profile),patch.dict(os.environ,{'G3_REPLAY_R137_COMBINED':'1','G3_REPLAY_R137':'1','G3_REPLAY_PROFILE':profile}):
    self.assertIsNone(m.main())

class PrivateReleaseQuarantineTests(unittest.TestCase):
 def test_software_release_retains_storage_after_failed_broker_revoke(self):
  spec=importlib.util.spec_from_file_location('release_quarantine_replay',ROOT/'tests/g3_vidmm_replay.py')
  m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
  original=m.generate
  def generate(*args,**kwargs):
   source=original(*args,**kwargs)
   scenarios=(ROOT/'tests/g3_vidmm_replay_scenarios.c').read_text()
   private=(ROOT/'tests/g3_r137_private_combined.c').read_text()
   marker='  t->Escape.pPrivateDriverData=q;t->Escape.PrivateDriverDataSize=sizeof(*q);'
   assert private.count(marker)==1
   private=private.replace(marker,marker+'\n  t->Escape.Flags.Value=0u;')
   return source.replace('#include "g3_vidmm_replay_scenarios.c"',
       scenarios.replace('#include "g3_r137_private_combined.c"',private))
  m.generate=generate
  for profile in ('16','64'):
   with self.subTest(profile=profile),patch.dict(os.environ,{
       'G3_REPLAY_R137_COMBINED':'1','G3_REPLAY_R137':'1','G3_REPLAY_PROFILE':profile,
       'G3_REPLAY_R137_QUARANTINE':'revoke'}):
    self.assertIsNone(m.main())

if __name__=='__main__':unittest.main()
