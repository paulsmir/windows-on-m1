"""F1-k production start policy (COMPLETENESS-F1-standalone-gpu-boot.md 4.C).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
1. The pure state machine (stale pending latches, error / clean stop /
   healthy timer clear, a latched adapter refuses, the unclean-boot counter
   latches at 3 and resets on a clean shutdown, policy 0 writes nothing).
2. Policy 0 must keep the bench's one-shot G3Armed path textually and
   behaviourally as before: the arm helpers are byte-identical, the arm call
   is reachable under the Arm decision only, and the executed StartDevice
   prefix consumes the arm exactly as before.
3. The Windows wiring calls every event at its lifecycle point (pending
   persisted and flushed before the decision returns, StartDevice return,
   clean StopDevice, timer drained before the context goes away).
4. F1K-STAMP (EXP2405): no start-policy registry write in the shutdown
   power path (the registry is closed there); the start attempt derives the
   orderly exit from Windows' ShutdownTime and persists the new stamp first
   in the same flush as Pending.
Host builds only; nothing here proves an MSVC or hardware result.
"""
import hashlib
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_apple_agx_pool_headroom import function
from test_g4_sysmem_broker import run

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers' / 'apple-agx' / 'shared'
RENDER = ROOT / 'drivers' / 'apple-agx' / 'render-admission'
LIFECYCLE = RENDER / 'src' / 'lifecycle.c'

# sha256 of the helper bodies at origin/gpu/memory-latency 8982b9c35 (before
# F1-k). Policy 0 must run exactly this code.
ARM_HELPERS = {
    'AdmissionConsumeGpuvaArm':
        'a2d3eeadb4ad8f559f85ac51d0fc86f7cb007812b68b3803bed87c6d3cdd0f2b',
    'AdmissionReportGpuvaArmConsumed':
        '115206662cba88dfc2064272210dfa7ea0723fe18fff2ce1c6839c839be96ac7',
}

ARM_GATE = '''  if (start_policy == AppleAgxStartPolicyDecisionArm &&
      !AdmissionConsumeGpuvaArm(context, L"G3Armed", TRUE)) {
    AdmissionRecordStartStage(context, AdmissionStartEntered,
                              STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
  }
'''

NEW_VALUES = ('Wom1StartPolicy', 'Wom1StartPending', 'Wom1StartRunning',
              'Wom1StartLatched', 'Wom1StartLatchReset',
              'Wom1StartUncleanCount', 'Wom1StartLatchCount',
              'Wom1StartPolicyAttempt', 'Wom1StartPolicyReturn',
              'Wom1StartPolicyStop', 'Wom1StartPolicyTimer',
              'Wom1StartPolicyIoStatus', 'Wom1StartShutdownStamp',
              'Wom1StartPolicyStamp')


class StartPolicyStateMachine(unittest.TestCase):
    def test_pure_state_machine(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'apple_agx_start_policy_test'
            subprocess.run([
                os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-Wconversion', '-Wsign-conversion',
                '-fsanitize=address,undefined', '-I', str(SHARED / 'include'),
                str(SHARED / 'tests' / 'apple_agx_start_policy_test.c'),
                str(SHARED / 'src' / 'apple_agx_start_policy.c'),
                '-o', str(binary)], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT, timeout=30)

    def test_shared_module_has_no_windows_headers(self):
        for path in (SHARED / 'src' / 'apple_agx_start_policy.c',
                     SHARED / 'include' / 'apple_agx_start_policy.h'):
            includes = re.findall(r'#include\s+[<"]([^>"]+)[>"]',
                                  path.read_text())
            self.assertEqual(includes, [] if path.suffix == '.h'
                             else ['apple_agx_start_policy.h'])

    def test_project_links_the_module(self):
        project = (RENDER / 'AppleAgxRenderAdmission.vcxproj').read_text()
        self.assertIn(r'..\shared\src\apple_agx_start_policy.c', project)
        self.assertIn(r'..\shared\include\apple_agx_start_policy.h', project)


class StartPolicyZeroIsTodaysArm(unittest.TestCase):
    def setUp(self):
        self.source = LIFECYCLE.read_text()
        self.start = function(self.source, 'AdmissionDdiStartDevice')
        self.gate = self.start[:self.start.index(
            'status = AdmissionInterruptStart(context);')]

    def test_arm_helpers_are_byte_identical(self):
        for name, digest in ARM_HELPERS.items():
            body = function(self.source, name)
            self.assertEqual(hashlib.sha256(body.encode()).hexdigest(), digest,
                             name + ' changed: policy 0 must stay today\'s arm')

    def test_arm_is_reachable_under_the_arm_decision_exactly_as_before(self):
        self.assertEqual(self.gate.count('AdmissionConsumeGpuvaArm(context, L"G3Armed", TRUE)'), 1)
        self.assertIn(ARM_GATE, self.gate)
        decide = self.gate.index('start_policy = AdmissionStartPolicyAttempt(context);')
        self.assertLess(self.gate.index('AdmissionG3FirmwareResourcesPresent(context)'), decide)
        self.assertLess(decide, self.gate.index(ARM_GATE))
        # Every non-Arm, non-Allow decision fails closed before GPU access.
        refuse = self.gate[self.gate.index(ARM_GATE) + len(ARM_GATE):]
        self.assertIn('start_policy != AppleAgxStartPolicyDecisionAllow', refuse)
        self.assertLess(refuse.index('return STATUS_NOT_SUPPORTED;'),
                        refuse.index('AdmissionSystemSegmentRequested(context)'))

    def test_executed_prefix_per_decision(self):
        boundary = 'status = AdmissionInterruptStart(context);'
        prefix = self.start[:self.start.index(boundary) + len(boundary)]
        run(FIXTURE + prefix + '\n(void)post_display_route; return status;\n}\n' + CASES)


class StartPolicyWiring(unittest.TestCase):
    def setUp(self):
        self.source = LIFECYCLE.read_text()

    def test_attempt_persists_and_flushes_before_allowing(self):
        event = function(self.source, 'AdmissionStartPolicyEvent')
        persist = function(self.source, 'AdmissionStartPolicyPersist')
        self.assertLess(persist.index('ZwSetValueKey'), persist.index('ZwFlushKey(Key)'))
        self.assertLess(persist.index('ZwDeleteValueKey'), persist.index('ZwFlushKey(Key)'))
        self.assertIn('STATUS_OBJECT_NAME_NOT_FOUND', persist)
        # Pending keeps its value (2 = scheduler faulted at T) on disk.
        self.assertIn('AppleAgxStartPolicyParsePending(present, dword, value)', event)
        self.assertNotIn('data = 1u;', persist)
        # The latch decision runs before any hardware access: the decision
        # helper touches only the registry (and m1n1's ARM_CONSUMED HVC after
        # the flush, on Allow only).
        for helper in ('AdmissionStartPolicyEvent', 'AdmissionStartPolicyPersist',
                       'AdmissionStartPolicyQuery', 'AdmissionStartPolicyAttempt'):
            body = function(self.source, helper)
            self.assertIsNone(re.search(r'READ_REGISTER|WRITE_REGISTER|MmMapIoSpace|Mmio', body), helper)
        self.assertLess(event.index('AppleAgxStartPolicyEvaluate(&state, Event'),
                        event.index('AdmissionStartPolicyPersist(key, Result,'))
        self.assertLess(event.index('AdmissionStartPolicyPersist(key, Result,'),
                        event.index('Result->Decision = AppleAgxStartPolicyDecisionRefuse'))
        self.assertLess(event.index('AdmissionStartPolicyPersist(key, Result,'),
                        event.index('PendingOwned = 1u'))
        # Policy 0: no registry access outside the start attempt.
        early = event[:event.index('IoOpenDeviceRegistryKey')]
        self.assertIn('Context->StartPolicy.Policy != APPLE_AGX_START_POLICY_CRASH_LATCH', early)
        attempt = function(self.source, 'AdmissionStartPolicyAttempt')
        self.assertLess(attempt.index('AdmissionStartPolicyEvent('),
                        attempt.index('AdmissionReportGpuvaArmConsumed(Context)'))

    def test_registered_start_device_handles_the_return(self):
        gated = function(self.source, 'AdmissionDdiStartDeviceGated')
        self.assertLess(gated.index('AdmissionDdiStartDevice(MiniportDeviceContext'),
                        gated.index('AdmissionStartPolicyReturned('))
        driver = (RENDER / 'src' / 'driver.c').read_text()
        self.assertIn('initialization.DxgkDdiStartDevice = AdmissionDdiStartDeviceGated;', driver)
        wrappers = (RENDER / 'include' / 'render_ddi_status_wrappers.h').read_text()
        self.assertIn('ADMISSION_STATUS_WRAP(1, AdmissionDdiStartDeviceGated,', wrappers)
        self.assertIn('Init->DxgkDdiStartDevice = AdmissionDdiStartDeviceGatedStatus;', wrappers)
        returned = function(self.source, 'AdmissionStartPolicyReturned')
        self.assertIn('AppleAgxStartPolicyEventFailed', returned)
        self.assertLess(returned.index('KeClearEvent'), returned.index('KeSetTimer'))

    def test_clean_stop_quiesces_first_and_clears_last(self):
        stop = function(self.source, 'AdmissionDdiStopDevice')
        self.assertLess(stop.index('AdmissionStartPolicyQuiesce(context);'),
                        stop.index('AdmissionScanoutStop(context)'))
        tail = stop[stop.index('AdmissionStartPolicyStopped(context);'):]
        self.assertEqual(tail.count('return '), 1)
        self.assertIn('return STATUS_SUCCESS;', tail)

    def test_remove_drains_the_timer_before_freeing(self):
        remove = function(self.source, 'AdmissionDdiRemoveDevice')
        self.assertLess(remove.index('AdmissionStartPolicyRelease(context);'),
                        remove.index('ExFreePoolWithTag(context'))
        quiesce = function(self.source, 'AdmissionStartPolicyQuiesce')
        self.assertLess(quiesce.index('KeCancelTimer'), quiesce.index('KeWaitForSingleObject'))
        self.assertIn('KeFlushQueuedDpcs', quiesce)

    def test_shutdown_power_path_writes_nothing(self):
        """EXP2405: SetPowerState(shutdown) runs after CmShutdownSystem; any
        registry write there returns STATUS_TOO_LATE and is lost."""
        power = function(self.source, 'AdmissionDdiSetPowerState')
        self.assertIsNone(re.search(r'StartPolicy|Zw\w+|IoOpenDeviceRegistryKey', power))
        self.assertIn('return STATUS_SUCCESS;', power)
        self.assertIsNone(re.search(r'AdmissionStartPolicyShutdown\b', self.source))
        self.assertNotIn('AppleAgxStartPolicyEventShutdown', self.source)

    def test_attempt_derives_orderly_exit_from_shutdown_time(self):
        event = function(self.source, 'AdmissionStartPolicyEvent')
        persist = function(self.source, 'AdmissionStartPolicyPersist')
        query = function(self.source, 'AdmissionStartPolicyQueryQword')
        read = function(self.source, 'AdmissionStartPolicyShutdownTime')
        self.assertIn(r'L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Windows"',
                      self.source)
        self.assertIn('#define ADMISSION_SHUTDOWN_TIME_VALUE L"ShutdownTime"', self.source)
        # REG_BINARY of exactly 8 bytes; anything else is unreadable.
        self.assertRegex(read, r'ADMISSION_SHUTDOWN_TIME_VALUE,\s+REG_BINARY, Value\)')
        self.assertIn('value->DataLength != sizeof(ULONGLONG)', query)
        self.assertIn('return STATUS_OBJECT_TYPE_MISMATCH;', query)
        self.assertIn('sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONGLONG)', query)
        # The opened key is closed on every path after a successful open.
        self.assertEqual(read.count('ZwOpenKey('), 1)
        self.assertEqual(read.count('ZwClose(key);'), 1)
        self.assertLess(read.index('ZwClose(key);'), read.rindex('return status;'))
        # Derived only at a policy-1 attempt, before the decision.
        derive = event.index('state.OrderlyExit = AppleAgxStartPolicyOrderlyExit(')
        self.assertLess(event.index('if (state.Policy == APPLE_AGX_START_POLICY_CRASH_LATCH)'),
                        derive)
        self.assertLess(derive, event.rindex('AppleAgxStartPolicyEvaluate(&state, Event'))
        self.assertIn('AppleAgxStartPolicyStampAction(Result->Decision, time_readable)', event)
        # The stamp goes first, in the same sequence and flush as Pending.
        self.assertLess(persist.index('ADMISSION_START_SHUTDOWN_STAMP'),
                        persist.index('for (index = 0u;'))
        self.assertLess(persist.index('REG_QWORD, &Stamp'), persist.index('ZwFlushKey(Key)'))
        self.assertIn('ADMISSION_START_POLICY_STAMP_RECEIPT', event)

    def test_timer_clears_only_while_scheduler_is_healthy(self):
        worker = function(self.source, 'AdmissionStartPolicyWorker')
        self.assertIn('AppleAgxStartPolicyEventTimer', worker)
        self.assertIn('SchedulerFaulted', worker)
        self.assertLess(worker.index('AdmissionStartPolicyEvent('),
                        worker.index('KeSetEvent'))

    def test_value_names_are_new(self):
        """No new device-key value may reuse a name another owner writes."""
        tracked = subprocess.run(['git', 'ls-files', 'drivers', 'tools', 'scripts', 'tests'],
                                 cwd=ROOT, capture_output=True, text=True, check=True).stdout.split()
        mine = {'drivers/apple-agx/render-admission/src/lifecycle.c',
                'drivers/apple-agx/render-admission/include/render_admission.h',
                'drivers/apple-agx/shared/include/apple_agx_start_policy.h',
                'drivers/apple-agx/shared/src/apple_agx_start_policy.c',
                'drivers/apple-agx/shared/tests/apple_agx_start_policy_test.c',
                'tests/test_render_start_policy.py'}
        for name in NEW_VALUES:
            self.assertIn('L"%s"' % name, self.source)
            for path in tracked:
                if path in mine:
                    continue
                try:
                    text = (ROOT / path).read_text(errors='ignore')
                except (IsADirectoryError, FileNotFoundError):
                    continue
                self.assertIsNone(re.search(r'\b' + name + r'\b', text),
                                  name + ' already used in ' + path)


FIXTURE = r'''
#include <assert.h>
#include <string.h>
#include <wchar.h>
#include "apple_agx_start_policy.h"
#define APPLE_AGX_GPUVA_G3_QUALIFICATION 1
#define _Use_decl_annotations_
#define TRUE 1
#define FALSE 0
#define NT_SUCCESS(s) ((s)>=0)
#define STATUS_PENDING 1
#define STATUS_INVALID_PARAMETER -1
#define STATUS_NOT_SUPPORTED -2
#define STATUS_DEVICE_NOT_READY -3
#define STATUS_INVALID_DEVICE_STATE -4
#define PASSIVE_LEVEL 0
#define AdmissionReceiptStartEntered 1
#define AdmissionStartEntered 1
#define AdmissionReceiptStartDeviceInfo 2
#define AdmissionStartDeviceInfo 2
#define RtlZeroMemory(p,n) memset(p,0,n)
typedef int NTSTATUS, BOOLEAN, APPLE_AGX_POST_DISPLAY_ROUTE;
typedef void *PVOID;
typedef unsigned long ULONG, *PULONG;
typedef struct {int unused;} DXGK_START_INFO, *PDXGK_START_INFO;
typedef struct {void *DeviceHandle; NTSTATUS (*DxgkCbGetDeviceInformation)(void *, int *);}
    DXGKRNL_INTERFACE, *PDXGKRNL_INTERFACE;
typedef struct {void *PhysicalDeviceObject; DXGK_START_INFO StartInfo;
 DXGKRNL_INTERFACE Interface; int InterfaceValid, DeviceInformation, Requested;
 struct {long StartDeviceCount; long long StartEntryQpc;} MpoGate;
} ADMISSION_CONTEXT;
static long InterlockedIncrement(long *p) {return ++*p;}
static long long AdmissionMpoGateQpc(ADMISSION_CONTEXT *c) {(void)c; return 1;}
static unsigned starts, arms, decisions, segment_reads;
static int firmware=1, armed=1, last_status;
static ULONG decision = AppleAgxStartPolicyDecisionArm;
static int KeGetCurrentIrql(void) {return 0;}
static int AdmissionSystemSegmentRequested(ADMISSION_CONTEXT *c) {
 assert(!starts); ++segment_reads; return c->Requested;
}
static NTSTATUS get_info(void *h,int *i) {(void)h;*i=7;return 0;}
static void AdmissionRecordDevice(void *p,int stage,int status) {(void)p;(void)stage;(void)status;}
static void AdmissionRecordStartStage(ADMISSION_CONTEXT *c,int stage,int status) {
 (void)c;(void)stage;last_status=status;
}
static void AdmissionRecordTranslatedResources(ADMISSION_CONTEXT *c) {(void)c;}
static int AdmissionG3FirmwareResourcesPresent(ADMISSION_CONTEXT *c) {(void)c;return firmware;}
static ULONG AdmissionStartPolicyAttempt(ADMISSION_CONTEXT *c) {
 (void)c; assert(!arms && !starts); ++decisions; return decision;
}
static int AdmissionConsumeGpuvaArm(ADMISSION_CONTEXT *c,const wchar_t *name,int report) {
 (void)c;assert(!wcscmp(name,L"G3Armed") && report && decisions);++arms;return armed;
}
static NTSTATUS AdmissionInterruptStart(ADMISSION_CONTEXT *c) {(void)c;++starts;return 123;}
'''

CASES = r'''
static NTSTATUS Start(void) {
 ADMISSION_CONTEXT c={0};DXGK_START_INFO start={0};
 DXGKRNL_INTERFACE i={NULL,get_info};ULONG sources=99,children=99;
 NTSTATUS s;
 starts=arms=decisions=segment_reads=0;
 s=AdmissionDdiStartDevice(&c,&start,&i,&sources,&children);
 if (s!=123) assert(!sources && !children && !starts);
 return s;
}
int main(void) {
 (void)KeGetCurrentIrql;
 /* Policy 0: today's sequence - decide, consume the arm, segment check,
  * first runtime start. Without the arm: refused before any start. */
 assert(Start()==123 && decisions==1 && arms==1 && segment_reads==1 && starts==1);
 armed=0;
 assert(Start()==STATUS_NOT_SUPPORTED && arms==1 && !segment_reads && !starts);
 assert(last_status==STATUS_NOT_SUPPORTED);
 /* Policy 1 allowed: no arm is read even when none exists. */
 decision=AppleAgxStartPolicyDecisionAllow;
 assert(Start()==123 && decisions==1 && !arms && segment_reads==1 && starts==1);
 /* Latched / invalid / unrecorded: fail closed before the GPU, no arm. */
 decision=AppleAgxStartPolicyDecisionRefuse;
 assert(Start()==STATUS_NOT_SUPPORTED && !arms && !segment_reads && !starts);
 decision=AppleAgxStartPolicyDecisionNone;
 assert(Start()==STATUS_NOT_SUPPORTED && !arms && !starts);
 /* The resource-shape refusal still precedes the policy decision. */
 decision=AppleAgxStartPolicyDecisionAllow; firmware=0;
 assert(Start()==STATUS_DEVICE_NOT_READY && !decisions && !arms && !starts);
 return 0;
}
'''


if __name__ == '__main__':
    unittest.main()
