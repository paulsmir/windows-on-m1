"""Exercise real buffered copy handlers without Windows global GPU-idle entry."""
import importlib.util
import os
from pathlib import Path
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
EXTRA=r'''
  /* Software entry must copy exactly the requested range and release handles. */
  escape.Flags.Value=0;
  q->Operation=APPLE_AGX_G3_COPY_QUERY;q->Offset=0;q->TransferBytes=0;
  q->MappingGeneration=q->ProcessGeneration=0;
  expect_ok("software copy QUERY",AdmissionDdiEscape(&a,&escape));
  q->Operation=APPLE_AGX_G3_COPY_UPLOAD;q->Offset=0xff9;q->TransferBytes=64;
  for(UINT i=0;i<64;++i)q->Data[i]=(unsigned char)(i^0xa7);
  expect_ok("software copy UPLOAD",AdmissionDdiEscape(&a,&escape));
  assert(!memcmp(local_cpu+0x100ff9,q->Data,64));
  q->Operation=APPLE_AGX_G3_COPY_DOWNLOAD;memset(q->Data,0,64);
  expect_ok("software copy DOWNLOAD",AdmissionDdiEscape(&a,&escape));
  for(UINT i=0;i<64;++i)assert(q->Data[i]==(unsigned char)(i^0xa7));
  assert(r145_references==0);
  /* Preserve compatibility for the old unnecessarily synchronized entry. */
  escape.Flags.Value=1;
  q->Operation=APPLE_AGX_G3_COPY_QUERY;q->Offset=0;q->TransferBytes=0;
  q->MappingGeneration=q->ProcessGeneration=0;
  expect_ok("legacy synchronized copy QUERY",AdmissionDdiEscape(&a,&escape));
'''
COPY_PTE_VISITS=r'''
  /* The real lookup must traverse two graph slots and one broker bucket,
   * independent of unrelated parent/shadow list length. */
  AppleAgxGpuvaG3LookupStatsReset();copy_pte_visits=0;
  const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *indexed=AdmissionG3CopyPte(p,0x10000);
  assert(indexed && (indexed->Flags&APPLE_AGX_GPUVA_G3_VALID));
  assert(AppleAgxGpuvaG3LookupStatsVisits()==2);
  assert(copy_pte_visits==1);
'''
class SoftwareCopyTests(unittest.TestCase):
 def test_real_query_upload_download_both_page_profiles(self):
  spec=importlib.util.spec_from_file_location('g3_software_replay',ROOT/'tests/g3_vidmm_replay.py')
  m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
  original=m.generate
  def generate(*args,**kwargs):
   source=original(*args,**kwargs)
   source=source.replace('#include "g3_vidmm_replay_shim.h"',
      '#include "g3_vidmm_replay_shim.h"\nstatic unsigned copy_pte_visits;\n#define ADMISSION_G3_COPY_PTE_VISIT() (++copy_pte_visits)')
   scenarios=(ROOT/'tests/g3_vidmm_replay_scenarios.c').read_text()
   copy=(ROOT/'tests/g3_r145_copy_cases.c').read_text()
   marker='  expect_ok("R145 copy query",AdmissionDdiEscape(&a,&escape));'
   assert copy.count(marker)==1
   copy=copy.replace(marker,marker+EXTRA)
   marker='  expect_ok("R145 local publication",AdmissionGpuvaG3BuildPagingBuffer(&a,&x));'
   assert copy.count(marker)==1
   copy=copy.replace(marker,marker+COPY_PTE_VISITS)
   scenarios=scenarios.replace('#include "g3_r145_copy_cases.c"',copy)
   return source.replace('#include "g3_vidmm_replay_scenarios.c"',scenarios)
  m.generate=generate
  for profile in ('16','64'):
   with self.subTest(profile=profile),patch.dict(os.environ,{'G3_REPLAY_R145':'1','G3_REPLAY_PROFILE':profile,'G3_REPLAY_COPY_PTE_VISITS':'1'}):
    self.assertIsNone(m.main())
if __name__=='__main__':unittest.main()
