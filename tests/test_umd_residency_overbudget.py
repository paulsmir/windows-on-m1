"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
After idle residency is exhausted, the UMD must ask VidMm once with
CantTrimFurther instead of treating the current process budget as a hard cap.
Exercise the production retry and submission-hold path, including hard refusal.
"""
import unittest
import test_umd_persistent_residency as persistent
BODY = persistent.BODY

class Overbudget(unittest.TestCase):
    run_replay = persistent.PersistentResidency.run_replay
    def test_exhausted_trim_requests_vidmm_overbudget_once(self):
        code = BODY[:BODY.index('int main(){')]
        code += r'''
static unsigned calls, overbudget;
static bool hard_limit;
static HRESULT budget(HANDLE,D3DDDI_MAKERESIDENT *r) {
 ++calls;assert(!r->Flags.MustSucceed);assert(r->NumAllocations==1);
 if(r->Flags.CantTrimFurther) {
  ++overbudget;
  if(!hard_limit){r->PagingFenceValue=91;return E_PENDING;}
 }
 r->NumBytesToTrim=32u<<20;return E_OUTOFMEMORY;
}
int main(){
 for(unsigned mode=0;mode<4;++mode)
 for(unsigned scenario=0;scenario<3;++scenario){
  const char *values[]={nullptr,"0","1","00"};
  if(values[mode]) setenv("APPLE_AGX_UMD_CANT_TRIM_FURTHER",values[mode],1);
  else unsetenv("APPLE_AGX_UMD_CANT_TRIM_FURTHER");
  bool disabled=mode==1;
  CB cb={budget,ev};ADMISSION_UMD_DEVICE d={};d.Magic=ADMISSION_UMD_DEVICE_MAGIC;
  d.PagingQueue=(HANDLE)1;d.KernelCallbacks=&cb;
  auto &s=d.ScreenBuffers[0];s.Active=1;s.Token=10;s.KernelAllocation=0x100;s.Bytes=65536;s.StagingAllocation=0x200;s.CanonicalGpuVa=0x10000;
  // A held sibling cannot be sacrificed to satisfy the budget.
  auto &held=d.ScreenBuffers[1];held.Active=held.Resident=1;held.SubmissionHolds=1;held.KernelAllocation=0x101;held.Bytes=65536;
  if(scenario==2){auto &idle=d.ScreenBuffers[2];idle.Active=idle.Resident=1;idle.KernelAllocation=0x102;idle.Token=12;idle.Bytes=65536;}
  hard_limit=scenario==1 || disabled;calls=overbudget=evicted=0;
  uint64_t token=10,fence=0;
  assert(make_resident(&d,&token,1,&fence)==(hard_limit?0:2));
  assert(overbudget==(disabled?0u:1u));
  assert(calls==(scenario==2?3u:2u)-(disabled?1u:0u));
  assert(held.Resident && held.SubmissionHolds==1);
  assert(s.Resident==!hard_limit && s.SubmissionHolds==!hard_limit);
  assert(fence==(hard_limit?0u:91u));
  assert(evicted==(scenario==2?1u:0u));
 }
 puts("bounded overbudget: PASS");
}
'''
        self.run_replay(code, 'bounded overbudget: PASS')

    def test_last_bounded_trim_exhausts_idle_set_before_fallback(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Hitting the trim-round cap must not skip the overbudget request.
        The final trim selects all eligible idle slots; holds remain excluded
        by the separately exercised production trim policy.
        """
        import subprocess
        import tempfile
        from pathlib import Path
        code = BODY[:BODY.index('@@FUNCS@@')]
        code += r'''
static unsigned trims,calls;
static bool disabled;
static ULONGLONG trim_residency(ADMISSION_UMD_DEVICE *,ULONGLONG want,UINT *n){
 ++trims;assert(want==(trims==ADMISSION_UMD_TRIM_ROUNDS && !disabled?~0ull:1ull));
 *n=1;return 1;
}
''' + persistent.function(persistent.SRC.read_text(),'residency_request')
        code += r'''
static HRESULT budget(HANDLE,D3DDDI_MAKERESIDENT *r){
 ++calls;assert(!r->Flags.MustSucceed);
 if(r->Flags.CantTrimFurther){assert(trims==ADMISSION_UMD_TRIM_ROUNDS);r->PagingFenceValue=13;return E_PENDING;}
 r->NumBytesToTrim=1;return E_OUTOFMEMORY;
}
int main(){
 for(unsigned mode=0;mode<2;++mode){
 disabled=mode!=0;trims=calls=0;
 setenv("APPLE_AGX_UMD_CANT_TRIM_FURTHER",disabled?"0":"1",1);
 CB cb={budget,ev};ADMISSION_UMD_DEVICE d={};d.KernelCallbacks=&cb;
 D3DDDI_MAKERESIDENT r={};D3DKMT_HANDLE h=1;UINT priority=0;
 assert(residency_request(&d,&r,&h,1,&priority)==(disabled?E_OUTOFMEMORY:E_PENDING));
 assert(calls==ADMISSION_UMD_TRIM_ROUNDS+1 && r.PagingFenceValue==(disabled?0u:13u));
 }
}
'''
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'t.cpp').write_text(code)
            z=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',
                '-I'+str(persistent.ROOT/'drivers/apple-agx/render-admission/umd/include'),
                str(p/'t.cpp'),'-o',str(p/'t')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
            z=subprocess.run([str(p/'t')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
