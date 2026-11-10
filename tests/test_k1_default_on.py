"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
K1 (Wom1CrossProcessRenderPairs) is default on after EXP2325/EXP2326: an absent
device value must enable it, an explicit DWORD 0 must keep it off, an explicit 1
keeps it on, and an unreadable or malformed value must not disable it. The
prerequisites (C1 Wom1CrossProcessPairs and the manager slots) still gate it in
every case. The test executes the production registry reader and the production
K1 assignment from backend_platform_windows.c.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_render_job_depth2_exp2047 import WORKER, RENDER, SHARED, section


def source():
    text = WORKER.read_text()
    start = text.index('  runtime->CrossProcessRenderPairs = runtime->CrossProcessPairs &&')
    k1 = text[start:text.index(';', start) + 1]
    reader = section(text[text.rindex('static BOOLEAN AdmissionPlatformSwitchDisabled('):],
                     'static BOOLEAN AdmissionPlatformSwitchDisabled(') + '\n}\n'
    enabled = section(text[text.rindex('static BOOLEAN AdmissionPlatformSwitchEnabled('):],
                      'static BOOLEAN AdmissionPlatformSwitchEnabled(') + '\n}\n'
    return r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define TRUE 1
#define FALSE 0
#define NT_SUCCESS(s) ((s) == 0)
#define PLUGPLAY_REGKEY_DEVICE 1
#define KEY_QUERY_VALUE 1
#define REG_DWORD 4
#define KeyValuePartialInformation 2
typedef unsigned int ULONG;
typedef unsigned long long ULONGLONG;
typedef unsigned char UCHAR, BOOLEAN;
typedef void *HANDLE;
typedef const wchar_t *PCWSTR;
typedef const wchar_t *UNICODE_STRING;
typedef struct { ULONG TitleIndex, Type, DataLength; UCHAR Data[4]; }
 KEY_VALUE_PARTIAL_INFORMATION, *PKEY_VALUE_PARTIAL_INFORMATION;
typedef struct { void *PhysicalDeviceObject; } ADMISSION_CONTEXT;
static int mode; /* 0/1 explicit DWORD, 2 absent, 3 wrong type, 4 short, 5 value 2, 6 key open fails */
static int IoOpenDeviceRegistryKey(void *device, unsigned kind, unsigned access, HANDLE *key) {
 assert(device && kind==PLUGPLAY_REGKEY_DEVICE && access==KEY_QUERY_VALUE);
 if(mode==6)return -1; *key=device; return 0;
}
static void RtlInitUnicodeString(UNICODE_STRING *name, PCWSTR value) {*name=value;}
static int ZwQueryValueKey(HANDLE key, UNICODE_STRING *name, unsigned kind,
 PKEY_VALUE_PARTIAL_INFORMATION out, unsigned size, ULONG *bytes) {
 assert(key && kind==KeyValuePartialInformation && size>=sizeof(*out));
 if(wcscmp(*name,L"Wom1CrossProcessRenderPairs") || mode==2)return -1;
 out->Type=mode==3?1:REG_DWORD; out->DataLength=mode==4?1:sizeof(ULONG);
 ULONG value=mode==5?2u:(mode==1?1u:0u); memcpy(out->Data,&value,sizeof(value)); *bytes=sizeof(*out); return 0;
}
static void ZwClose(HANDLE key) {assert(key);}
''' + enabled + reader + r'''
typedef struct { BOOLEAN CrossProcessPairs, ManagerSlots, CrossProcessRenderPairs; } Runtime;
int main(int argc, char **argv) {
 assert(argc==4); mode=atoi(argv[1]);
 ADMISSION_CONTEXT ctx={(void *)1}; ADMISSION_CONTEXT *Context=&ctx;
 Runtime rt={(BOOLEAN)atoi(argv[2]),(BOOLEAN)atoi(argv[3]),0}; Runtime *runtime=&rt;
 (void)AdmissionPlatformSwitchEnabled; (void)AdmissionPlatformSwitchDisabled;
''' + k1 + r'''
 printf("%u\n",(unsigned)runtime->CrossProcessRenderPairs); return 0;
}
'''


class K1DefaultOn(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='k1-default-')
        cls.addClassCleanup(cls.tmp.cleanup)
        path = Path(cls.tmp.name)
        (path / 'model.c').write_text(source())
        cmd = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
               '-fsanitize=address,undefined', '-I' + str(RENDER / 'include'),
               '-I' + str(SHARED / 'include'), str(path / 'model.c'), '-o', str(path / 'model')]
        built = subprocess.run(cmd, capture_output=True, text=True)
        if built.returncode:
            raise AssertionError(built.stderr)
        cls.model = path / 'model'

    def k1(self, mode, pairs=1, slots=1):
        run = subprocess.run([str(self.model), str(mode), str(pairs), str(slots)],
                             capture_output=True, text=True, check=True)
        return int(run.stdout)

    def test_absent_and_one_enable_zero_disables(self):
        self.assertEqual(self.k1(2), 1)
        self.assertEqual(self.k1(1), 1)
        self.assertEqual(self.k1(0), 0)

    def test_unreadable_or_malformed_values_keep_the_default(self):
        for mode in (3, 4, 5, 6):
            with self.subTest(mode=mode):
                self.assertEqual(self.k1(mode), 1)

    def test_prerequisites_still_gate_it(self):
        for mode in (2, 1, 0):
            for pairs, slots in ((0, 1), (1, 0), (0, 0)):
                with self.subTest(mode=mode, pairs=pairs, slots=slots):
                    self.assertEqual(self.k1(mode, pairs, slots), 0)


if __name__ == '__main__':
    unittest.main()
