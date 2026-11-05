"""Actual resident-copy accessors: scattered Windows pages and logical bounds."""
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SHARED=ROOT/'drivers/apple-agx/shared'


class PresentMemoryTests(unittest.TestCase):
    def test_scattered_page_reads_short_copy_dummy_and_logical_bounds(self):
        source=(ROOT/'drivers/apple-agx/render-admission/src/memory_runtime_windows.c').read_text()
        structure=re.search(r'typedef struct _ADMISSION_PRESENT_MEMORY_IO.*?} ADMISSION_PRESENT_MEMORY_IO;',source,re.S).group(0)
        functions='\n'.join(re.search(r'static int '+name+r'\(.*?^}',source,re.S|re.M).group(0)
                            for name in ['AdmissionPresentReadMemory','AdmissionPresentWriteMemory'])
        shim=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "apple_agx_software_aperture.h"
#define PAGE_SIZE 4096u
#define RtlCopyMemory memcpy
#define STATUS_SUCCESS 0
#define STATUS_INVALID_ADDRESS (-1)
#define STATUS_PARTIAL_COPY (-2)
#define NT_SUCCESS(x) ((x)>=0)
#define MM_COPY_MEMORY_PHYSICAL 1u
typedef unsigned UINT;typedef unsigned long long ULONGLONG;typedef long long LONGLONG;
typedef size_t SIZE_T;typedef int NTSTATUS;typedef unsigned char *PUCHAR;
typedef struct {void *CpuAddress;ULONGLONG Bytes;} ADMISSION_LOCAL_MEMORY_VIEW;
typedef struct {struct {APPLE_AGX_SOFTWARE_APERTURE Aperture;} Memory;} ADMISSION_CONTEXT;
typedef union {struct {LONGLONG QuadPart;} PhysicalAddress;} MM_COPY_ADDRESS;
static unsigned char pages[2][4096];static unsigned calls,short_second;
static ULONGLONG addresses[2];static SIZE_T lengths[2];
static NTSTATUS MmCopyMemory(void *destination,MM_COPY_ADDRESS source,SIZE_T bytes,unsigned flags,SIZE_T *copied){
 ULONGLONG p=(ULONGLONG)source.PhysicalAddress.QuadPart;unsigned page;SIZE_T offset;
 assert(flags==1 && calls<2);
 if(p>=0x210000 && p<0x211000){page=0;offset=(SIZE_T)(p-0x210000);}
 else {assert(p>=0x930000 && p<0x931000);page=1;offset=(SIZE_T)(p-0x930000);}
 assert(bytes<=4096-offset);addresses[calls]=p;lengths[calls]=bytes;++calls;
 *copied=(short_second && calls==2)?bytes-1:bytes;memcpy(destination,pages[page]+offset,*copied);return 0;
}
'''
        cases=r'''
int main(void){
 ADMISSION_CONTEXT c={0};APPLE_AGX_SOFTWARE_APERTURE_ENTRY entries[2];
 APPLE_AGX_U64 physical[2]={0x210000,0x930000};
 assert(AppleAgxSoftwareApertureInitialize(&c.Memory.Aperture,entries,2)==0);
 assert(AppleAgxSoftwareApertureMap(&c.Memory.Aperture,0,physical,2)==0);
 pages[0][4093]=0xab;pages[0][4094]=0xbc;pages[0][4095]=0xcd;
 for(unsigned i=0;i<6;++i)pages[1][i]=(unsigned char)(i+1);
 ADMISSION_PRESENT_MEMORY_IO io={0};io.Adapter=&c;io.SourceSegment=1;io.SourceBytes=100;io.ApertureOffset=4090;
 unsigned char out[12];memset(out,0xa5,sizeof(out));
 const unsigned char expected[9]={0xab,0xbc,0xcd,1,2,3,4,5,6};
 assert(AdmissionPresentReadMemory(&io,3,out,9));assert(memcmp(out,expected,9)==0 && out[9]==0xa5);
 assert(calls==2 && addresses[0]==0x210ffd && lengths[0]==3 && addresses[1]==0x930000 && lengths[1]==6);
 calls=0;short_second=1;assert(!AdmissionPresentReadMemory(&io,3,out,9));assert(io.Status==STATUS_PARTIAL_COPY);
 calls=0;short_second=0;io.Status=0;
 assert(AppleAgxSoftwareApertureUnmap(&c.Memory.Aperture,1,1,0xab0000)==AppleAgxSoftwareApertureOk);
 assert(!AdmissionPresentReadMemory(&io,3,out,9));assert(calls==1 && io.Status==STATUS_INVALID_ADDRESS);
 unsigned char local[12];memset(local,7,sizeof(local));io.SourceSegment=2;io.SourceBytes=8;io.Source.CpuAddress=local;
 assert(AdmissionPresentReadMemory(&io,4,out,4));assert(out[0]==7 && out[3]==7);
 assert(!AdmissionPresentReadMemory(&io,7,out,2));
 io.Destination.CpuAddress=local;io.Destination.Bytes=8;memset(out,9,4);
 assert(!AdmissionPresentWriteMemory(&io,5,out,4));assert(local[8]==7);
 assert(AdmissionPresentWriteMemory(&io,5,out,3));assert(local[4]==7 && local[5]==9 && local[7]==9 && local[8]==7);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            program=Path(tmp)/'memory.c';program.write_text(shim+structure+functions+cases);binary=Path(tmp)/'memory'
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined','-I',str(SHARED/'include'),str(program),
                            str(SHARED/'src/apple_agx_software_aperture.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
