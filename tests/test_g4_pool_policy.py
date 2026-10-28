"""rc109: segment 3 follows the machine policy VidMm enforces."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_apple_agx_pool_headroom import function

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'

HARNESS = r'''
#include "render_pool_demand.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>
typedef unsigned long long ULONGLONG;
typedef long long LONGLONG;
typedef unsigned int ULONG;
typedef unsigned char UCHAR;
typedef unsigned short WCHAR;
typedef int BOOLEAN;
typedef int NTSTATUS;
typedef void VOID;
typedef void *HANDLE;
typedef const wchar_t *PCWSTR;
typedef struct { const wchar_t *text; } UNICODE_STRING;
typedef struct { LONGLONG QuadPart; } LARGE_INTEGER;
typedef struct { ULONG TitleIndex, Type, DataLength; UCHAR Data[1]; }
    KEY_VALUE_PARTIAL_INFORMATION, *PKEY_VALUE_PARTIAL_INFORMATION;
typedef struct { LARGE_INTEGER LastWriteTime; ULONG TitleIndex, NameLength; WCHAR Name[1]; }
    KEY_BASIC_INFORMATION, *PKEY_BASIC_INFORMATION;
typedef struct { const UNICODE_STRING *Name; ULONG Attributes; } OBJECT_ATTRIBUTES;
typedef struct { void *PhysicalDeviceObject; } ADMISSION_CONTEXT;
#define TRUE 1
#define FALSE 0
#define REG_DWORD 4
#define PLUGPLAY_REGKEY_DEVICE 1
#define KEY_QUERY_VALUE 1
#define KeyValuePartialInformation 2
#define KeyBasicInformation 0
#define OBJ_CASE_INSENSITIVE 0x40
#define OBJ_KERNEL_HANDLE 0x200
#define STATUS_BUFFER_OVERFLOW ((NTSTATUS)0x80000005)
#define NT_SUCCESS(s) ((s) >= 0)
#define RtlZeroMemory(p, n) memset((p), 0, (n))
#define InitializeObjectAttributes(a, n, f, r, s) \
    ((a)->Name = (n), (a)->Attributes = (f), (void)(r), (void)(s))

/* The machine key: values present, their data, LastWriteTime. */
static int key_exists = 1, key_query = 0;
static ULONG percent = 75, have_percent = 1, have_max = 0, max_percent = 80;
static ULONG have_cpu = 0, cpu_mib = 16384;
static LONGLONG written = 1000, now_time = 10000000, interrupt = 1000000;
/* The device key: Wom1PoolPercent. */
static int device_opens = 1, have_pool = 0;
static ULONG pool_value = 75, pool_kind = REG_DWORD;
static int opens, closes;
static int machine = 0x1234, device = 0x5678;
static NTSTATUS ZwOpenKey(HANDLE *key, int access, OBJECT_ATTRIBUTES *a) {
    assert(access == KEY_QUERY_VALUE);
    assert(a->Attributes == (OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE));
    assert(!wcscmp(a->Name->text, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet"
                                  L"\\Control\\GraphicsDrivers\\MemoryManager"));
    if (!key_exists) return -1;
    ++opens; *key = &machine; return 0;
}
static NTSTATUS IoOpenDeviceRegistryKey(void *pdo, int which, int access, HANDLE *key) {
    assert(pdo && which == PLUGPLAY_REGKEY_DEVICE && access == KEY_QUERY_VALUE);
    if (!device_opens) return -1;
    ++opens; *key = &device; return 0;
}
static void RtlInitUnicodeString(UNICODE_STRING *s, const wchar_t *text) { s->text = text; }
static NTSTATUS ZwQueryValueKey(HANDLE key, UNICODE_STRING *name, int which,
                                PKEY_VALUE_PARTIAL_INFORMATION value, size_t cap,
                                ULONG *bytes) {
    ULONG data = 0, kind = REG_DWORD, present = 0;
    assert(which == KeyValuePartialInformation);
    assert(cap >= offsetof(KEY_VALUE_PARTIAL_INFORMATION, Data) + 4);
    if (key == &machine) {
        if (!wcscmp(name->text, L"SystemPartitionCommitLimitPercentage"))
            present = have_percent, data = percent;
        else if (!wcscmp(name->text, L"SystemPartitionCommitLimitPercentageMax"))
            present = have_max, data = max_percent;
        else if (!wcscmp(name->text, L"SystemPartitionCpuOnlyMemory"))
            present = have_cpu, data = cpu_mib;
        else assert(!"unexpected machine value");
    } else {
        assert(key == &device && !wcscmp(name->text, L"Wom1PoolPercent"));
        present = have_pool; data = pool_value; kind = pool_kind;
    }
    if (!present) return -1;
    value->Type = kind; value->DataLength = 4;
    memcpy(value->Data, &data, 4); *bytes = 16;
    return 0;
}
static NTSTATUS ZwQueryKey(HANDLE key, int which, PKEY_BASIC_INFORMATION info,
                           size_t cap, ULONG *bytes) {
    assert(key == &machine && which == KeyBasicInformation && cap >= sizeof(*info));
    info->LastWriteTime.QuadPart = written; *bytes = sizeof(*info);
    return key_query;
}
static void KeQuerySystemTime(LARGE_INTEGER *t) { t->QuadPart = now_time; }
static ULONGLONG KeQueryInterruptTime(void) { return (ULONGLONG)interrupt; }
static void ZwClose(HANDLE key) { assert(key == &machine || key == &device); ++closes; }
'''

MAIN = r'''
static ULONGLONG segment(void) {
    const ULONGLONG m = 1ULL << 20;
    ADMISSION_POOL_GRAPHICS_POLICY policy;
    ADMISSION_CONTEXT ctx = { &ctx };
    ULONG flags = 0;
    AdmissionPoolGraphicsPolicyRead(&policy, &flags);
    ULONG pool = AdmissionPoolPercent(&ctx, &flags);
    assert(opens == closes);
    return AdmissionPoolDemandSegmentBytesShare(7218794496ULL, 4096 * m, 4608 * m,
        ADMISSION_POOL_DEMAND_APERTURE_RESERVE,
        AdmissionPoolDemandShareBytes(7218794496ULL, &policy, pool));
}
int main(void) {
    const ULONGLONG m = 1ULL << 20;
    ADMISSION_POOL_GRAPHICS_POLICY policy;
    ULONG flags = 0;
    /* SystemPartitionCommitLimitPercentage=75 set before this boot (boot at
     * now - interrupt = 9000000; written 1000): the 4 GiB window binds. */
    assert(segment() == 4096 * m);
    AdmissionPoolGraphicsPolicyRead(&policy, &flags);
    assert(policy.Percent == 75 && policy.MaxPercent == 80 &&
           policy.CpuOnlyMiB == 16384 && !policy.WrittenAfterBoot);
    assert(flags == (ADMISSION_POOL_POLICY_FLAG_KEY | ADMISSION_POOL_POLICY_FLAG_PERCENT));
    /* Written during this boot: VidMm still holds the boot policy. */
    written = 9000000;
    assert(segment() == 3136 * m);
    flags = 0;
    AdmissionPoolGraphicsPolicyRead(&policy, &flags);
    assert(policy.WrittenAfterBoot && (flags & ADMISSION_POOL_POLICY_FLAG_AFTER_BOOT));
    written = 8999999;
    assert(segment() == 4096 * m);
    /* An unreadable write time is not trusted; a truncated name is fine. */
    key_query = -1;
    assert(segment() == 3136 * m);
    key_query = STATUS_BUFFER_OVERFLOW;
    assert(segment() == 4096 * m);
    key_query = 0;
    /* Windows' defaults (no key, or no percentage): the pre-rc109 size. */
    key_exists = 0;
    assert(segment() == 3136 * m);
    flags = 0;
    AdmissionPoolGraphicsPolicyRead(&policy, &flags);
    assert(flags == 0 && policy.Percent == 50 && !policy.WrittenAfterBoot);
    key_exists = 1; have_percent = 0;
    assert(segment() == 3136 * m);
    have_percent = 1;
    /* Max and CpuOnly are read too: a Max below P never caps P. */
    have_max = 1; max_percent = 60;
    assert(segment() == 4096 * m);
    have_max = 0;
    /* Wom1PoolPercent: 0 is the kill switch, other values bound the share,
     * a wrong type keeps the default, a device key failure the default. */
    have_pool = 1; pool_value = 0;
    assert(segment() == 3136 * m);
    flags = 0;
    ADMISSION_CONTEXT ctx = { &ctx };
    assert(AdmissionPoolPercent(&ctx, &flags) == 0 &&
           flags == ADMISSION_POOL_POLICY_FLAG_POOL_PERCENT);
    pool_value = 55; /* 3786 - 256 MiB in 64 MiB steps */
    assert(segment() == 3520 * m);
    pool_value = 1000;
    assert(AdmissionPoolPercent(&ctx, &flags) == 100);
    assert(segment() == 4096 * m);
    pool_kind = 3; pool_value = 0;
    assert(AdmissionPoolPercent(&ctx, &flags) == ADMISSION_POOL_PERCENT_DEFAULT);
    pool_kind = REG_DWORD;
    device_opens = 0;
    assert(AdmissionPoolPercent(&ctx, &flags) == ADMISSION_POOL_PERCENT_DEFAULT);
    ctx.PhysicalDeviceObject = NULL;
    assert(AdmissionPoolPercent(&ctx, &flags) == ADMISSION_POOL_PERCENT_DEFAULT);
    return 0;
}
'''


class PoolPolicy(unittest.TestCase):
    def test_policy_reader_never_outruns_vidmm(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        VidMm reads MemoryManager\\SystemPartitionCommitLimitPercentage once, at
        boot, and refuses the adapter when segment 3 exceeds the share it
        computed. A value written during this boot (or with an unreadable write
        time) must not enlarge the segment; absent policy and Wom1PoolPercent=0
        must give the pre-rc109 3136 MiB on J313; every opened key is closed.
        This executes the real Windows readers with registry substitutes.
        """
        source = (RENDER / 'src/pool_windows.c').read_text()
        code = HARNESS + '\n'.join(function(source, name) for name in (
            'AdmissionPoolReadDword', 'AdmissionPoolGraphicsPolicyRead',
            'AdmissionPoolPercent')) + MAIN
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp) / 'policy.c', Path(tmp) / 'policy'
            src.write_text(code)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                            '-Wextra', '-Werror', '-Wno-unused-parameter',
                            '-ftrivial-auto-var-init=pattern',
                            '-fsanitize=address,undefined',
                            '-I', str(RENDER / 'include'), str(src),
                            str(RENDER / 'src/render_pool_demand.c'), '-o', str(exe)],
                           check=True)
            subprocess.run([str(exe)], check=True, timeout=10)

    def test_start_publishes_the_policy_size_and_records_its_inputs(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        The base (kill-switch) size must come from the default formula and
        never exceed the published size, and the receipt must carry the inputs
        that sized the segment so a hardware run can tell policy from default.
        """
        source = (RENDER / 'src/pool_windows.c').read_text()
        start = function(source, 'AdmissionPoolDemandStart')
        self.assertIn('AdmissionPoolGraphicsPolicyRead(&policy, &policyFlags);', start)
        self.assertIn('share = AdmissionPoolDemandShareBytes(ram, &policy, percent);', start)
        self.assertIn('baseBytes = AdmissionPoolSegmentBytes(ram,', start)
        self.assertIn('if (bytes < baseBytes) baseBytes = bytes;', start)
        self.assertIn('pool->BaseSegmentBytes = baseBytes;', start)
        self.assertLess(start.index('AdmissionPoolGraphicsPolicyRead('),
                        start.index('ExAllocatePool2('))
        for field in ('GraphicsShareBytes', 'BaseSegmentBytes', 'PoolPercent',
                      'PolicyPercent', 'PolicyMaxPercent', 'PolicyCpuOnlyMiB',
                      'PolicyFlags'):
            self.assertIn(f'receipt.{field} = ', source)


if __name__ == '__main__':
    unittest.main()
