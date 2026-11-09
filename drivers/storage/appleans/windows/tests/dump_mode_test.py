#!/usr/bin/env python3
"""Boot-start and crash-dump contract for native Windows boot (ANS010, ANS-DUMP-MODE DUMP2).

Invariants (each one is a way to lose the disk or hang the bugcheck if broken):
- HwFindAdapter checks the structure length before it reads DumpMode, and takes the dump
  branch before it parses resources or reaches a transport of the live path.
- The dump copy dereferences, copies, maps and marks nothing before the live instance's
  hand-over record passed validation; only crash and mark-memory modes are accepted.
- Nothing the dump copy can reach uses the registry, pool, a DPC, a timer, a spin lock, the
  error log, passive initialisation, a fresh common buffer or the EL2 broker transport.
- The dump copy never writes the lent queue memory before the controller is proven stopped
  (ansd_init_takeover, not ansd_init) and never allocates or clears a new buffer.
- The live instance hands the record out only through SRB_FUNCTION_DUMP_POINTERS, sealed, and
  only when the feature bit that makes Storport send that SRB is set.
- The INF stays boot-start in the SCSI Miniport group, or the installed C: cannot boot.
"""
from pathlib import Path
import re
WIN = Path(__file__).resolve().parents[1]
src = (WIN / 'driver.c').read_text()
direct = (WIN / 'direct.c').read_text()
def fn(text, signature):
    start = text.index(signature)
    return text[start:text.index('\n}\n', start)]
def before(body, first, *later):
    i = body.index(first)
    for name in later:
        j = body.find(name)
        assert j < 0 or i < j, (first, name)
    return i
BANNED = ('Registry', 'ExAllocate', 'IssueDpc', 'InitializeDpc', 'RequestTimerCall',
          'AcquireSpinLock', 'StorPortLogError', 'EnablePassiveInitialization',
          'GetUncachedExtension', 'Wire.', 'wire_', 'operation(', 'AppleAnsNativeResources',
          'AppleAnsNativeInitialize')
find = fn(src, 'ULONG AppleAnsFindAdapter(')
length = before(find, 'ConfigInfo->Length < sizeof(*ConfigInfo)', 'ConfigInfo->DumpMode', 'a->')
branch = before(find, 'if (dump) return dump_find(a, ConfigInfo);', 'AccessRanges',
                'struct ans_resources', 'ans_resources_', 'AppleAnsNative', 'AppleAnsDirect',
                'adapter_limits')
assert length < branch
assert 'if (!dump && a->Signature == ANS_MAGIC' in find  # the dump extension is never trusted
dump_find = fn(src, 'static ULONG dump_find(')
before(dump_find, 'if (!ans_dump_context_valid(c, APPLE_ANS_DIRECT_BYTES)) return SP_RETURN_NOT_FOUND;',
       'c->', '= *c', 'dump_ranges(', 'AppleAnsDirect', 'StorPortMarkDumpMemory',
       'ans_dump_context_receipt(', 'SP_RETURN_FOUND :')
assert set(re.findall(r'DUMP_MODE_\w+', dump_find)) == {'DUMP_MODE_MARK_MEMORY', 'DUMP_MODE_CRASH'}
assert re.search(r'mode == DUMP_MODE_CRASH && \(flags & ANS_DUMP_SEEN_RANGES\)', dump_find)
dump_start = fn(src, 'static BOOLEAN dump_start(')
before(dump_start, 'a->DumpMode != DUMP_MODE_CRASH', 'AppleAnsNativeStart(')
before(dump_start, 'AppleAnsNativeStart(', 'AppleAnsDirectDumpDrain(a);')
dump_init = fn(direct, 'BOOLEAN AppleAnsDirectDumpInitialize(')
assert 'ansd_init_takeover(' in dump_init and 'ansd_init(' not in dump_init
for token in ('RtlZeroMemory', 'direct_admin(a, 6', 'direct_admin(a, 9'):
    assert token not in dump_init, token  # no clearing, no Identify, no Set Features
drain = fn(direct, 'VOID AppleAnsDirectDumpDrain(')
assert 'polls == DIRECT_DUMP_POLLS' in drain and 'direct_fault(a)' in drain  # bounded
for name, body in (('dump_find', dump_find), ('dump_start', dump_start),
                   ('dump_ranges', fn(src, 'static BOOLEAN dump_ranges(')),
                   ('dump_init', dump_init), ('drain', drain),
                   ('carve', fn(direct, 'static BOOLEAN direct_carve(')),
                   ('carve_pa', fn(direct, 'static BOOLEAN carve_pa(')),
                   ('create_io', fn(direct, 'static BOOLEAN direct_create_io('))):
    for banned in BANNED:
        assert banned not in body, (name, banned)
resources = fn(direct, 'BOOLEAN AppleAnsDirectResources(')
before(resources, 'if (a->DumpMode) {', 'GetUncachedExtension', 'RtlZeroMemory', 'ansd_init(')
init = fn(src, 'BOOLEAN AppleAnsInitialize(')
before(init, 'if (a->DumpMode)', 'AppleAnsNativeInitialize', 'EnablePassiveInitialization',
       'StorPortLogError')
for signature in ('BOOLEAN AppleAnsStartIo(', 'BOOLEAN AppleAnsResetBus('):
    before(fn(src, signature), '->DumpMode)', 'StorPortAcquireSpinLock', 'AppleAnsNativeReset')
interrupt = fn(src, 'BOOLEAN AppleAnsInterrupt(')
before(interrupt, '->DumpMode) return FALSE;', 'AppleAnsNativeInterrupt')
control = fn(src, 'SCSI_ADAPTER_CONTROL_STATUS AppleAnsAdapterControl(')
restart = control[control.index('ControlType == ScsiRestartAdapter'):]
before(restart, 'if (a->DumpMode) return ScsiAdapterControlUnsuccessful;', 'AppleAnsNativeInitialize')
native = (WIN / 'native.c').read_text()
diag = fn(native, 'VOID AppleAnsDiag(')
before(diag, 'if (a->DumpMode) return;', 'StorPortLogError')
assert re.search(r'FeatureSupport = [^;]*STOR_FEATURE_DUMP_POINTERS', src)
assert 'STOR_FEATURE_DUMP_RESUME_CAPABLE' not in src  # hibernation resume stays unsupported
ps = native.index('static BOOLEAN dump_pointers(')
pointers = native[ps:native.index('\n}\n', ps)]
seal = pointers.index('ans_dump_context_seal(')
refuse = pointers.index('if(!sealed) { AppleAnsComplete(a,srb,SRB_STATUS_INVALID_REQUEST,0);return TRUE; }')
publish = pointers.index('p->MiniportPrivateDumpData=&a->DumpContext;')
assert seal < refuse < publish, 'record published before it is sealed'
assert 'a->Direct && a->Ready && a->Common' in pointers[:seal]
assert native.count('MiniportPrivateDumpData=') == 1
assert 'if(srb->Function==SRB_FUNCTION_DUMP_POINTERS) return dump_pointers(a,srb);' in native
inf = (WIN / 'AppleAns.inf').read_text()
for line in ('ServiceType=1', 'StartType=0', 'LoadOrderGroup=SCSI Miniport', 'ErrorControl=1'):
    assert re.search(r'^' + re.escape(line) + r'\s*$', inf, re.M), line
print('AppleAns boot-start/dump-mode contract PASS')
