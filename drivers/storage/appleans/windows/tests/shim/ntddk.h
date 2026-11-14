/* SPDX-License-Identifier: MIT */
/* Host stand-in for <ntddk.h>: only what the AppleAns miniport sources use. Test code. */
#ifndef SHIM_NTDDK_H
#define SHIM_NTDDK_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef void VOID, *PVOID;
typedef unsigned char UCHAR, *PUCHAR, BOOLEAN, *PBOOLEAN;
typedef char CHAR, *PCHAR;
typedef unsigned short USHORT;
typedef uint32_t ULONG, *PULONG;
typedef int32_t LONG;
typedef uint64_t ULONGLONG, ULONG64, *PULONG64;
typedef int64_t LONGLONG;
typedef uintptr_t ULONG_PTR;
typedef size_t SIZE_T;
typedef union { struct { ULONG LowPart; LONG HighPart; } u; int64_t QuadPart; } LARGE_INTEGER, *PLARGE_INTEGER;
typedef LARGE_INTEGER PHYSICAL_ADDRESS;
#define TRUE 1
#define FALSE 0
#define _Use_decl_annotations_
#define DECLSPEC_NORETURN __attribute__((noreturn))
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define C_ASSERT(e) _Static_assert(e, #e)
#define RTL_SIZEOF_THROUGH_FIELD(t, f) (offsetof(t, f) + sizeof(((t *)0)->f))
#define RtlZeroMemory(p, n) memset((p), 0, (n))
#define RtlCopyMemory(d, s, n) memcpy((d), (s), (n))
#define MemoryBarrier() ((void)0)
#define YieldProcessor() shim_yield()
#define PASSIVE_LEVEL 0
#define DISPATCH_LEVEL 2
#define HIGH_LEVEL 15
#define METHOD_BUFFERED 0
#define FILE_READ_ACCESS 1
#define FILE_DEVICE_SCSI 0x1b
#define CTL_CODE(t, f, m, a) (((t) << 16) | ((a) << 14) | ((f) << 2) | (m))
#define STATUS_NO_SUCH_DEVICE 0xC000000Eu
typedef enum { MmNonCached = 0, MmCached = 1 } MEMORY_CACHING_TYPE;
void shim_yield(void);
LONG InterlockedIncrement(volatile LONG *v);
LONG InterlockedExchange(volatile LONG *v, LONG n);
PHYSICAL_ADDRESS MmGetPhysicalAddress(PVOID va);
#endif
