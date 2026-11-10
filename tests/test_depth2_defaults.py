"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Absent device values must select the EXP2109 accepted configuration, while an
explicit DWORD zero remains an opt-out. Execute the production registry readers
and startup assignments so absence cannot be mistaken for zero, prerequisites
cannot be bypassed, and diagnostics/unproven classes cannot become implicit.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_render_job_depth2_exp2047 import WORKER, RENDER, SHARED, section

# Independent expected policy, including the retained legacy selectors.
ON = ('ManagerSlots', 'JobBuildAhead', 'JobDepth2', 'JobDepth2Caps',
      'NotifierFlight', 'RcDepth2', 'Depth2General', 'Depth2TaOn3d',
      'Depth2TaOnCompute', 'Depth2Prelude', 'Depth2ComputeOnCompute')
# dbd498a80 (EXP2129-2133): prelude pairs and compute-on-compute default on.
OFF = ('RcReceipt', 'JobDiagnostics',
       'JobDepth2Kick', 'JobDepth2KickOn3d', 'ManagerSaveOnEvict')
FIELDS = ('slots', 'ahead', 'depth', 'caps', 'flight', 'rc', 'general',
          'classes', 'kick', 'prelude', 'receipt', 'diagnostics', 'save',
          'manager_depth', 'abort_suffix')
ACCEPTED = (1, 1, 1, 15, 1, 1, 1, 30, 1, 1, 0, 0, 1, 1, 1)


def source():
    text = WORKER.read_text()
    start = text.index('  runtime->ManagerSlots =')
    config = text[start:text.index('  RtlZeroMemory(&runtime->RcCurrent', start)]
    start = text.index('  runtime->Initdata.RenderSharedMemory.NotifierFlight =')
    notifier = text[start:text.index('  if (!AppleAgxPlatformProviderInitialize(', start)]
    readers = '\n'.join(section(text[text.rindex('static BOOLEAN '+name+'('):], 'static BOOLEAN '+name+'(')+'\n}\n'
        for name in ('AdmissionPlatformSwitchEnabled', 'AdmissionPlatformSwitchDisabled'))
    return r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "render_job_window.h"
#include "apple_agx_render_shared_memory.h"
#define TRUE 1
#define FALSE 0
#define APPLE_AGX_GPUVA_G3_QUALIFICATION 1
#define NT_SUCCESS(s) ((s) == 0)
#define PLUGPLAY_REGKEY_DEVICE 1
#define KEY_QUERY_VALUE 1
#define REG_DWORD 4
#define KeyValuePartialInformation 2
/* WDK-width types; no host unsigned-long substitution for ULONG. */
typedef unsigned int ULONG;
typedef unsigned long long ULONGLONG;
typedef unsigned char UCHAR, BOOLEAN;
typedef void *HANDLE;
typedef const wchar_t *PCWSTR;
typedef const wchar_t *UNICODE_STRING;
typedef struct { ULONG TitleIndex, Type, DataLength; UCHAR Data[4]; }
 KEY_VALUE_PARTIAL_INFORMATION, *PKEY_VALUE_PARTIAL_INFORMATION;
typedef struct { void *PhysicalDeviceObject; ULONG G3RetainedSlots, G3CrossProcessPairs; } ADMISSION_CONTEXT;
static const wchar_t *selected;
static int mode;
static unsigned opened, closed;
static int IoOpenDeviceRegistryKey(void *device, unsigned kind, unsigned access, HANDLE *key) {
 assert(device && kind==PLUGPLAY_REGKEY_DEVICE && access==KEY_QUERY_VALUE);
 if(mode==6)return -1;
 ++opened;*key=device;return 0;
}
static void RtlInitUnicodeString(UNICODE_STRING *name, PCWSTR value) {*name=value;}
static int ZwQueryValueKey(HANDLE key, UNICODE_STRING *name, unsigned kind,
 PKEY_VALUE_PARTIAL_INFORMATION out, unsigned size, ULONG *bytes) {
 assert(key && kind==KeyValuePartialInformation && size>=sizeof(*out));
 if(!selected || wcscmp(*name,selected) || mode==2)return -1; /* absent */
 out->Type=mode==3?1:REG_DWORD;out->DataLength=mode==4?1:sizeof(ULONG);
 ULONG value=mode==5?2u:(mode==1?1u:0u);
 memcpy(out->Data,&value,sizeof(value));*bytes=sizeof(*out);return 0;
}
static void ZwClose(HANDLE key) {assert(key);++closed;}
''' + readers + r'''
static APPLE_AGX_BOOL AdmissionEvictG4Manager(void *c,const APPLE_AGX_RENDER_MANAGER_KEY *k,
 APPLE_AGX_U32 f,const APPLE_AGX_EXP208_RELOCATION_OBJECT *o) {
 (void)c;(void)k;(void)f;(void)o;return 1;
}
typedef struct {
 BOOLEAN ManagerSlots,RcDepth2,RcReceiptEnabled,RcExportPending,Depth2General,Depth2Prelude;
 BOOLEAN JobAhead,JobDepth2,JobDepth2Kick,JobDiagnostics;
 BOOLEAN FwRxDrain; /* 125bddd1: class C, default on */
 BOOLEAN SubmitBracket; /* EXP2270: diagnostic, default off */
 BOOLEAN IrqCompletion; /* IRQ-completion step 2, class C, default on (inert without the ACPI line) */
 BOOLEAN PresentTsEnabled;
 ULONG Depth2FallbackDrill,Depth2FallbackDrillCount; /* be8c6f7f: diagnostic, default 0 */
 ULONG Depth2FallbackDrillBuilt,Depth2FallbackDrillBuiltCount; /* S4a drill, default 0 */
 unsigned Depth2Capabilities,Depth2KickClasses;
 struct { APPLE_AGX_RENDER_SHARED_MEMORY_OWNER RenderSharedMemory; } Initdata;
 struct { struct {APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG Config;} QueueProvider;} Provider;
 unsigned char CrossProcessPairs; /* C1: Wom1CrossProcessPairs, default off */
 ADMISSION_JOB_WINDOW_XPROC_RECEIPT XprocReceipt; unsigned long long XprocExportQpc;
} Runtime;
/* the Wom1Depth2FallbackDrill DWORD reader: absent in every default case */
static BOOLEAN AdmissionPlatformDwordValue(ADMISSION_CONTEXT *c,PCWSTR n,ULONG *v) {
 (void)c;(void)n;(void)v;return FALSE;
}
static void configure(Runtime *runtime,ADMISSION_CONTEXT *Context) {
''' + notifier + config + r'''
}
int main(int argc,char **argv) {
 (void)AdmissionPlatformSwitchDisabled;
 assert(argc==3);wchar_t name[80];assert(mbstowcs(name,argv[1],80)<80);
 selected=name;mode=atoi(argv[2]);
 ADMISSION_CONTEXT context={(void *)1,0u,0u};if(mode==7)context.PhysicalDeviceObject=NULL;
 Runtime r={0};configure(&r,&context);
 assert(opened==closed);
 printf("%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n",
 (unsigned)r.ManagerSlots,(unsigned)r.JobAhead,(unsigned)r.JobDepth2,r.Depth2Capabilities,
 (unsigned)r.Initdata.RenderSharedMemory.NotifierFlight,(unsigned)r.RcDepth2,
 (unsigned)r.Depth2General,r.Depth2KickClasses,(unsigned)r.JobDepth2Kick,
 (unsigned)r.Depth2Prelude,(unsigned)r.RcReceiptEnabled,(unsigned)r.JobDiagnostics,
 (unsigned)r.Initdata.RenderSharedMemory.ManagerSaveOnEvict,
 (unsigned)r.Initdata.RenderSharedMemory.ManagerDepth2,
 (unsigned)r.Provider.QueueProvider.Config.AbortFaultSuffix);
 return 0;
}
'''


class Depth2Defaults(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='q20-defaults-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.path = Path(cls.tmp.name)
        (cls.path/'model.c').write_text(source())
        for ahead in (0, 1):
            cmd = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                   '-ftrivial-auto-var-init=pattern', '-fsanitize=address,undefined',
                   f'-DADMISSION_JOB_AHEAD_BUILD={ahead}', '-I'+str(RENDER/'include'),
                   '-I'+str(SHARED/'include'), str(cls.path/'model.c'),
                   str(RENDER/'src/render_job_window.c'), '-o', str(cls.path/f'model{ahead}')]
            built = subprocess.run(cmd, capture_output=True, text=True)
            if built.returncode:
                raise AssertionError(built.stderr)

    def resolve(self, name='', mode=2, ahead=1):
        run = subprocess.run([str(self.path/f'model{ahead}'), 'Wom1'+name, str(mode)],
                             capture_output=True, text=True, check=True)
        return tuple(map(int, run.stdout.split()))

    def expected(self, **changes):
        values = dict(zip(FIELDS, ACCEPTED))
        values.update(changes)
        return tuple(values[f] for f in FIELDS)

    def test_absent_and_explicit_one_select_accepted_defaults(self):
        for switch in ON:
            for mode in (2, 1):
                with self.subTest(switch=switch, mode=mode):
                    self.assertEqual(self.resolve(switch, mode), ACCEPTED)

    def test_each_zero_preserves_its_opt_out_and_dependencies(self):
        cases = {
            'ManagerSlots': dict(slots=0, general=0, prelude=0, caps=13, classes=8, save=0, manager_depth=0, abort_suffix=0),
            'JobBuildAhead': dict(ahead=0, depth=0, caps=0, general=0, prelude=0, classes=8, kick=0, save=0, manager_depth=0, abort_suffix=0),
            'JobDepth2': dict(depth=0, caps=0, general=0, prelude=0, classes=8, kick=0, save=0, manager_depth=0, abort_suffix=0),
            'JobDepth2Caps': dict(caps=0, general=0, prelude=0, classes=8, kick=0, save=0, manager_depth=0, abort_suffix=0),
            'NotifierFlight': dict(flight=0, caps=13, general=0, prelude=0, classes=8, kick=0, save=0, manager_depth=0, abort_suffix=0),
            'RcDepth2': dict(rc=0),  # General subsumes it only while General is effective.
            'Depth2General': dict(general=0, prelude=0, caps=13, classes=8, save=0, manager_depth=0, abort_suffix=0),
            'Depth2TaOn3d': dict(classes=26),
            'Depth2TaOnCompute': dict(classes=28),
            'Depth2Prelude': dict(prelude=0),
            'Depth2ComputeOnCompute': dict(classes=14),
        }
        for switch, changes in cases.items():
            with self.subTest(switch=switch):
                self.assertEqual(self.resolve(switch, 0), self.expected(**changes))

    def test_unproven_classes_diagnostics_and_legacy_selectors_stay_opt_in(self):
        enabled = {'RcReceipt': dict(receipt=1), 'JobDiagnostics': dict(diagnostics=1)}
        for switch in OFF:
            for mode in (2, 0, 1):
                with self.subTest(switch=switch, mode=mode):
                    self.assertEqual(self.resolve(switch, mode),
                                     self.expected(**enabled.get(switch, {})) if mode==1 else ACCEPTED)

    def test_invalid_or_unreadable_settings_use_feature_defaults(self):
        for switch in ON+OFF:
            for mode in (3, 4, 5, 6, 7):
                with self.subTest(switch=switch, mode=mode):
                    self.assertEqual(self.resolve(switch, mode), ACCEPTED)

    def test_compile_time_disabled_build_ahead_never_enables_depth_two(self):
        self.assertEqual(self.resolve(ahead=0), self.expected(
            ahead=0, depth=0, caps=0, rc=0, general=0, prelude=0, classes=0,
            kick=0, save=0, manager_depth=0, abort_suffix=0))


if __name__ == '__main__':
    unittest.main()
