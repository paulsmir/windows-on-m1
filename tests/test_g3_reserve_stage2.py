"""Exercise the production stage-2 walker, with host-owned page tables."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

class ReserveStage2Test(unittest.TestCase):
    def test_normal_rw_mapping_attributes(self):
        s = (ROOT / 'm1n1_windows/src/hv_vm.c').read_text()
        self.assertIn('bool hv_ipa_is_normal_rw', s)
        macros = s[s.index('#define PAGE_SIZE'):s.index('uint64_t vaddr_bits;')]
        walker = s[s.index('static u64 hv_pt_walk_impl'):s.index('//\n// Translate a guest-physical')]
        check = s[s.index('bool hv_ipa_is_normal_rw'):s.index('#define CHECK_RN')]
        c = '''#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t u64;
#define BIT(n) (1ULL << (n))
#define MASK(n) (BIT(n)-1)
#define GENMASK(h,l) ((~0ULL << (l)) & (~0ULL >> (63-(h))))
#define FIELD_GET(m,v) (((v)&(m)) >> __builtin_ctzll(m))
#define dprintf(...) ((void)0)
''' + macros + '\nuint64_t vaddr_bits = 36; static u64 *hv_Ltop;\n' + walker + check + '''
int main(void) {
 hv_Ltop = aligned_alloc(0x4000,0x4000); memset(hv_Ltop,0,0x4000);
 u64 *l3 = aligned_alloc(0x4000,0x4000); memset(l3,0,0x4000);
 hv_Ltop[0] = 0x800000000ULL | PTE_ATTRIBUTES | PTE_VALID;
 assert(hv_ipa_is_normal_rw(0x1000));
 hv_Ltop[0] &= ~PTE_S2AP_RW; assert(!hv_ipa_is_normal_rw(0x1000));
 hv_Ltop[0] |= PTE_S2AP_RW; hv_Ltop[0] &= ~PTE_MEMATTR_UNCHANGED;
 assert(!hv_ipa_is_normal_rw(0x1000));
 hv_Ltop[0] = (u64)l3 | PTE_VALID | PTE_TYPE;
 l3[0] = 0x800000000ULL | PTE_ATTRIBUTES | PTE_VALID | PTE_TYPE;
 assert(hv_ipa_is_normal_rw(0x3fff));
 l3[0] &= ~PTE_VALID; assert(!hv_ipa_is_normal_rw(0x1000));
 free(l3); free(hv_Ltop); return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            src=Path(d)/'test.c'; src.write_text(c); exe=Path(d)/'test'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
