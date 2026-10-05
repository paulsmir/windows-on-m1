from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
PEI=ROOT/'mu/Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c'

class MuHobReserveTest(unittest.TestCase):
    def test_real_hob_walk_rejects_live_allocations_and_firmware_volumes(self):
        s=PEI.read_text()
        self.assertIn('STATIC BOOLEAN AgxLocalReserveHobsSafe',s)
        body=s[s.index('STATIC BOOLEAN AgxLocalReserveHobsSafe'):s.index('//Borrowed from ArmPlatformPkg')]
        prefix='''#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#define STATIC static
#define IN
#define TRUE true
#define FALSE false
#define CONST const
typedef uint64_t UINT64; typedef size_t UINTN; typedef bool BOOLEAN;
#define AGX_LOCAL_RESERVE_BYTES 0x40000000ULL
#define EFI_HOB_TYPE_MEMORY_ALLOCATION 2
#define EFI_HOB_TYPE_FV 5
#define EFI_HOB_TYPE_FV2 9
#define EFI_HOB_TYPE_FV3 12
#define EFI_HOB_TYPE_HANDOFF 1
#define EFI_HOB_TYPE_END_OF_HOB_LIST 0xffff
typedef struct { unsigned short HobType,HobLength; unsigned Reserved; } HDR;
typedef struct { HDR Header; struct { char Name[16]; UINT64 MemoryBaseAddress,MemoryLength; } AllocDescriptor; } ALLOC;
typedef struct { HDR Header; UINT64 BaseAddress,Length; } FV;
typedef struct { HDR Header; unsigned Version,BootMode; UINT64 EfiMemoryTop,EfiMemoryBottom,EfiFreeMemoryTop,EfiFreeMemoryBottom,EfiEndOfHobList; } PHIT;
typedef union { void *Raw; HDR *Header; ALLOC *MemoryAllocation; FV *FirmwareVolume; PHIT *HandoffInformationTable; } EFI_PEI_HOB_POINTERS;
#define END_OF_HOB_LIST(h) ((h).Header->HobType == EFI_HOB_TYPE_END_OF_HOB_LIST)
#define GET_NEXT_HOB(h) ((void *)((char *)(h).Raw + (h).Header->HobLength))
static unsigned char list[1024];
static void *GetHobList(void) { return list; }
'''
        suffix='''
int main(void) {
 const UINT64 b=0x8e0000000ULL;
 PHIT *phit=(void *)list; phit->Header=(HDR){1,sizeof(PHIT),0};
 phit->EfiMemoryBottom=0x850000000ULL; phit->EfiMemoryTop=0x854000000ULL;
 ALLOC *a=(void *)(list+sizeof(PHIT)); a->Header=(HDR){2,sizeof(ALLOC),0};
 a->AllocDescriptor.MemoryBaseAddress=0x851000000ULL; a->AllocDescriptor.MemoryLength=0x4000;
 FV *fv=(void *)((char *)a+sizeof(ALLOC)); fv->Header=(HDR){5,sizeof(FV),0};
 fv->BaseAddress=0x852000000ULL; fv->Length=0x200000;
 HDR *end=(void *)((char *)fv+sizeof(FV)); *end=(HDR){0xffff,sizeof(HDR),0};
 assert(AgxLocalReserveHobsSafe(b));
 a->AllocDescriptor.MemoryBaseAddress=b+0x3ffff000; assert(!AgxLocalReserveHobsSafe(b));
 a->AllocDescriptor.MemoryBaseAddress=0x851000000ULL;
 unsigned types[]={5,9,12};
 for(unsigned i=0;i<3;++i){fv->Header.HobType=types[i];fv->BaseAddress=b+0x20000000;assert(!AgxLocalReserveHobsSafe(b));}
 fv->BaseAddress=0x852000000ULL;
 phit->EfiMemoryTop=b+0x1000; assert(!AgxLocalReserveHobsSafe(b));
 phit->EfiMemoryTop=0x854000000ULL;
 a->AllocDescriptor.MemoryBaseAddress=UINT64_MAX-1; assert(!AgxLocalReserveHobsSafe(b));
 return 0;
}
'''
        header=(ROOT/'mu/Silicon/Apple/T810XFamilyPkg/Include/Library/AgxLocalReserveValidation.h').read_text()
        helper=header[header.index('static inline BOOLEAN\nAgxLocalReserveDisjoint'):header.index('static inline BOOLEAN\nAgxLocalReserveRangeValid')]
        with tempfile.TemporaryDirectory() as d:
            src=Path(d)/'test.c';src.write_text(prefix+helper+body+suffix);exe=Path(d)/'test'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_stack_is_published_before_reservation(self):
        s=(ROOT/'mu/Silicon/Apple/AppleSiliconPkg/PrePi/PrePi.c').read_text()
        self.assertLess(s.index('BuildStackHob('),s.index('Status = MemoryPeim('))
