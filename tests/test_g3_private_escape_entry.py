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
  if(q->Operation==APPLE_AGX_G3_PRIVATE_RELEASE) {
    t->Escape.Flags.Value=0u;
    assert(AdmissionDdiEscape(t->Adapter,&t->Escape)==STATUS_INVALID_PARAMETER);
  }
  t->Escape.Flags.Value=q->Operation==APPLE_AGX_G3_PRIVATE_RELEASE ? 1u : 0u;
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
   scenarios=scenarios.replace('#include "g3_r137_private_combined.c"',private)
   return source.replace('#include "g3_vidmm_replay_scenarios.c"',scenarios)
  m.generate=generate
  for profile in ('16','64'):
   with self.subTest(profile=profile),patch.dict(os.environ,{'G3_REPLAY_R137_COMBINED':'1','G3_REPLAY_R137':'1','G3_REPLAY_PROFILE':profile}):
    self.assertIsNone(m.main())
if __name__=='__main__':unittest.main()
