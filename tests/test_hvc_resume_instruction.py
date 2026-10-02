"""Execute the real handled-exception epilogue against architectural ELR values."""
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
M1N1 = ROOT / 'm1n1_windows/src'

class HvcResumeInstructionTests(unittest.TestCase):
    def test_hvc_preserves_next_instruction_and_traps_advance(self):
        source = (M1N1 / 'hv_exc.c').read_text()
        start = source.index('    if (handled) {', source.index('case ESR_EC_HVC:'))
        start = source.index('{', start) + 1
        end = source.index('\n    }', start)
        body = source[start:end]
        program = r'''
#include <assert.h>
#include <stdint.h>
#include "hv_guest_ipa_pa.h"
#define ESR_EC (0x3full << 26)
#define ESR_EC_HVC 0x16u
#define FIELD_GET(mask, value) (((value) & (mask)) >> 26)
struct exc_info { uint64_t elr, esr; };
static uint64_t returned_pc;
static void hv_wdt_breadcrumb(char c) {(void)c;}
static void hv_set_elr(uint64_t pc) {returned_pc=pc;}
static void hv_update_fiq(void) {}
static void finish_handled(struct exc_info *ctx) {
''' + body + r'''
}
int main(void) {
    /* HVC at0x1000 saves0x1004: that instruction consumes the return value. */
    struct exc_info hvc = {0x1004, 0x16ull << 26};
    finish_handled(&hvc);
    assert(returned_pc == 0x1004 && "HVC skipped its first return-value instruction");
    /* Trapped MSR saves the faulting instruction and must advance once. */
    struct exc_info msr = {0x2000, 0x18ull << 26};
    finish_handled(&msr);
    assert(returned_pc == 0x2004);
    struct exc_info smc_trap = {0x3000, 0x17ull << 26};
    finish_handled(&smc_trap);
    assert(returned_pc == 0x3004);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'resume.c';exe=Path(d)/'resume';c.write_text(program)
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-DHV_GUEST_IPA_PA_HOST_TEST','-I',str(M1N1),str(c),str(M1N1/'hv_guest_ipa_pa.c'),'-o',str(exe)],check=True,capture_output=True,text=True)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)

if __name__=='__main__':unittest.main()
