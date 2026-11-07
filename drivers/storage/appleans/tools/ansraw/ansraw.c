/* SPDX-License-Identifier: MIT */
/* ANS008 raw write/readback probe for WinPE. Usage:
 *   ansraw <disk> <first_lba> <count> <check|write|zero>
 * Interlocks (any failure exits nonzero before the first write):
 *  - 4096-byte logical sectors; GPT layout;
 *  - [first_lba, first_lba+count) is exactly one Microsoft Reserved partition;
 *  - write: the range is all zero before writing; zero: the range holds the
 *    pattern or zeros before writing.
 * check: hash only. write: pattern, flush, read back, compare. zero: zeros,
 * flush, read back. All I/O is unbuffered write-through in 128 KiB chunks. */
#include <windows.h>
#include <winioctl.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ansraw_pattern.h"
#define CHUNK_BLOCKS 32u
static const GUID MSR={0xe3c9e316,0x0b5c,0x4db8,{0x81,0x7d,0xf9,0x2d,0xf0,0x02,0x15,0xae}};
static HANDLE disk=INVALID_HANDLE_VALUE;
static int fail(const char *what)
{
    printf("ANSRAW FAIL %s (win32 %lu)\n",what,GetLastError());
    return 2;
}
static int io(BOOL write, ULONGLONG lba, void *buf, DWORD bytes)
{
    LARGE_INTEGER off;DWORD done=0;
    off.QuadPart=(LONGLONG)(lba*ANSRAW_BLOCK);
    if(!SetFilePointerEx(disk,off,NULL,FILE_BEGIN))return 0;
    if(write ? !WriteFile(disk,buf,bytes,&done,NULL) : !ReadFile(disk,buf,bytes,&done,NULL))return 0;
    return done==bytes;
}
enum kind { KIND_ZERO, KIND_PATTERN, KIND_OTHER };
/* Classify and hash the whole range. Returns 0 on I/O failure. */
static int scan(ULONGLONG first, ULONGLONG count, BYTE *buf, BYTE *ref, enum kind *k,
                BYTE sha[32], double *seconds, ULONGLONG *first_bad, ULONGLONG *first_nonzero)
{
    BCRYPT_ALG_HANDLE alg;BCRYPT_HASH_HANDLE h;LARGE_INTEGER f,t0,t1;
    int zero=1,pattern=1;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)||
       BCryptCreateHash(alg,&h,NULL,0,NULL,0,0))return 0;
    *first_bad=*first_nonzero=~0ULL;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&t0);
    for(ULONGLONG at=0;at<count;at+=CHUNK_BLOCKS){
        DWORD n=(DWORD)min(CHUNK_BLOCKS,count-at);
        if(!io(FALSE,first+at,buf,n*ANSRAW_BLOCK))return 0;
        BCryptHashData(h,buf,n*ANSRAW_BLOCK,0);
        for(DWORD i=0;i<n;i++){
            BYTE *b=buf+i*ANSRAW_BLOCK;int z=1;
            for(DWORD j=0;j<ANSRAW_BLOCK;j++)if(b[j]){z=0;break;}
            ansraw_fill_block(first+at+i,ref);
            int p=!memcmp(b,ref,ANSRAW_BLOCK);
            if(!z){zero=0;if(*first_nonzero==~0ULL)*first_nonzero=first+at+i;}
            if(!p){pattern=0;if(*first_bad==~0ULL)*first_bad=first+at+i;}
        }
    }
    QueryPerformanceCounter(&t1);*seconds=(double)(t1.QuadPart-t0.QuadPart)/(double)f.QuadPart;
    BCryptFinishHash(h,sha,32,0);BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(alg,0);
    *k=zero?KIND_ZERO:pattern?KIND_PATTERN:KIND_OTHER;return 1;
}
static void hex(const char *label,const BYTE sha[32])
{
    printf("%s ",label);for(int i=0;i<32;i++)printf("%02x",sha[i]);printf("\n");
}
static int fill(ULONGLONG first, ULONGLONG count, BYTE *buf, int pattern, double *seconds)
{
    LARGE_INTEGER f,t0,t1;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&t0);
    for(ULONGLONG at=0;at<count;at+=CHUNK_BLOCKS){
        DWORD n=(DWORD)min(CHUNK_BLOCKS,count-at);
        for(DWORD i=0;i<n;i++){
            if(pattern)ansraw_fill_block(first+at+i,buf+i*ANSRAW_BLOCK);
            else memset(buf+i*ANSRAW_BLOCK,0,ANSRAW_BLOCK);
        }
        if(!io(TRUE,first+at,buf,n*ANSRAW_BLOCK)){
            printf("ANSRAW write failed at lba %llu\n",first+at);return 0;
        }
    }
    if(!FlushFileBuffers(disk))return 0;
    QueryPerformanceCounter(&t1);*seconds=(double)(t1.QuadPart-t0.QuadPart)/(double)f.QuadPart;
    return 1;
}
static const char *name(enum kind k){return k==KIND_ZERO?"zero":k==KIND_PATTERN?"pattern":"other";}
int main(int argc, char **argv)
{
    if(argc!=5){puts("usage: ansraw <disk> <first_lba> <count> <check|write|zero>");return 1;}
    ULONGLONG first=_strtoui64(argv[2],NULL,0),count=_strtoui64(argv[3],NULL,0);
    const char *mode=argv[4];
    int writing=!strcmp(mode,"write")||!strcmp(mode,"zero");
    if(!writing&&strcmp(mode,"check"))return fail("mode");
    if(!count||count>16384)return fail("count");
    char path[64];snprintf(path,sizeof path,"\\\\.\\PhysicalDrive%s",argv[1]);
    disk=CreateFileA(path,GENERIC_READ|(writing?GENERIC_WRITE:0),FILE_SHARE_READ|FILE_SHARE_WRITE,
                     NULL,OPEN_EXISTING,FILE_FLAG_NO_BUFFERING|FILE_FLAG_WRITE_THROUGH,NULL);
    if(disk==INVALID_HANDLE_VALUE)return fail("open disk");
    DISK_GEOMETRY_EX geo;DWORD got;
    if(!DeviceIoControl(disk,IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,NULL,0,&geo,sizeof geo,&got,NULL))
        return fail("geometry");
    printf("ANSRAW disk %s bytes %lld sector %lu\n",argv[1],geo.DiskSize.QuadPart,geo.Geometry.BytesPerSector);
    if(geo.Geometry.BytesPerSector!=ANSRAW_BLOCK)return fail("sector size");
    DWORD lsize=sizeof(DRIVE_LAYOUT_INFORMATION_EX)+128*sizeof(PARTITION_INFORMATION_EX);
    DRIVE_LAYOUT_INFORMATION_EX *layout=calloc(1,lsize);
    if(!layout||!DeviceIoControl(disk,IOCTL_DISK_GET_DRIVE_LAYOUT_EX,NULL,0,layout,lsize,&got,NULL))
        return fail("layout");
    if(layout->PartitionStyle!=PARTITION_STYLE_GPT)return fail("not GPT");
    int matches=0;
    for(DWORD i=0;i<layout->PartitionCount;i++){
        PARTITION_INFORMATION_EX *p=&layout->PartitionEntry[i];
        if(p->PartitionStyle!=PARTITION_STYLE_GPT)continue;
        if((ULONGLONG)p->StartingOffset.QuadPart==first*ANSRAW_BLOCK &&
           (ULONGLONG)p->PartitionLength.QuadPart==count*ANSRAW_BLOCK &&
           IsEqualGUID(&p->Gpt.PartitionType,&MSR)){
            matches++;printf("ANSRAW target = partition %lu (Microsoft Reserved)\n",p->PartitionNumber);
        }
    }
    if(matches!=1)return fail("range is not exactly one Microsoft Reserved partition");
    BYTE *buf=VirtualAlloc(NULL,CHUNK_BLOCKS*ANSRAW_BLOCK,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    BYTE *ref=VirtualAlloc(NULL,ANSRAW_BLOCK,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!buf||!ref)return fail("alloc");
    enum kind k;BYTE sha[32];double s;ULONGLONG bad,nonzero;
    if(!scan(first,count,buf,ref,&k,sha,&s,&bad,&nonzero))return fail("pre-read");
    printf("ANSRAW before %s read_s %.3f\n",name(k),s);hex("ANSRAW before_sha256",sha);
    if(!writing){printf("ANSRAW RESULT check %s\n",name(k));return 0;}
    int pattern=!strcmp(mode,"write");
    if(pattern?k!=KIND_ZERO:k==KIND_OTHER)return fail("unexpected pre-write contents");
    if(!fill(first,count,buf,pattern,&s))return fail("write/flush");
    printf("ANSRAW wrote %s %llu blocks write_s %.3f MBps %.1f\n",pattern?"pattern":"zero",count,s,
           count*ANSRAW_BLOCK/1e6/s);
    if(!scan(first,count,buf,ref,&k,sha,&s,&bad,&nonzero))return fail("readback");
    printf("ANSRAW after %s read_s %.3f MBps %.1f\n",name(k),s,count*ANSRAW_BLOCK/1e6/s);
    hex("ANSRAW after_sha256",sha);
    if(k!=(pattern?KIND_PATTERN:KIND_ZERO)){printf("ANSRAW first mismatching lba %llu\n",pattern?bad:nonzero);return fail("readback mismatch");}
    printf("ANSRAW RESULT %s OK\n",mode);
    CloseHandle(disk);return 0;
}
