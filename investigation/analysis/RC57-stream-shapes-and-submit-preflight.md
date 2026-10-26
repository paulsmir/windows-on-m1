# RC57: high-count streams and fallible native-envelope validation

Offline `codex/rc57`, base `ecceb003`. The task's requested size-rejection
attribution is **not reproduced**. This change adds a bounded pre-submit
validation path and workload-shaped probes. It does not establish universal
late-submit containment or fix the originating firmware timeout.

## Evidence and corrected premise

Saved evidence root:
`.local/experiments/EXP1978-probes-ibv/evidence/` in the shared repository.
Exact decoded fields, UMD rows and SHA-256 values are in the RC57 handoff's
`EXP1978-evidence.json` and `EXP1978-fault-decoded.json`.

- `rec1978h/kmd-receipts-pre-disable.txt:186` records first G4 rejection:
  branch9/Parse, STATUS_INVALID_PARAMETER, ParseUnmapped, render access
  kind4/ordinal12, read1 byte at `0x1100270000`, metadata280 bytes, private
  version3 size480. Dispatch PID4 is the scheduler, **owner OS PID8620**.
- `rec1978h/app-logs/gm1978b-umd.log:10436,10444,10449,10454,10459`
  identifies **GravityMark PID8224**, pure indexed counted draws with
  MaxCount **0xea60 = 60000**. These are recording receipts. There is no
  post-draw counted-path row in the saved non-NUL prefix. Its absence does not
  locate the failure before draw replay. The saved prefix contains 48 single
  dispatch recordings and five counted indexed recordings; no uncounted
  multirecord draw is observed there (the new tests cover it independently).
- `rec1978h/kmd-receipts-pre-disable.txt:188` records a firmware **Timeout**,
  fence `0xb499`, PID8224, 26ms, with no valid GPU fault address. The raw
  FAULT_INFO register has VALID clear. Thus the first parser receipt cannot
  be assigned to this application's crashing submission. The task supplies
  0x119/P1=2/P2=c000000d; no debugger was run and no pointer parameter is
  assigned a guessed type.
- `render-admission/src/gpuva_g3_windows.c:159-193` records only the first
  failure. Later wrapper errors can have the same public status. That makes
  this saved first receipt insufficient for exact crash attribution.

No third historical comparison pass, complete experiment-ledger reread,
hardware, guest, builder, network or other-worktree access was used.

## What the high-count replay proves

`shared/src/apple_agx_g4_submit.c:231` bounds the **native metadata envelope**
to 4096 bytes. Generated VDM records are in separate BOs, not appended to this
envelope. `:95,123` validates the render stream base as a mapped address; it
does not read GPU-generated records. The combined compute stream extent is
checked at `:161-177`; timestamp mode and dependency headers are checked at
`:82-94,299-364`. The virtual DDI has no allocation-list field
(WDK26100 `d3dkmddi.h:5218-5233`), and the KMD explicitly records that at
`gpuva_g3_windows.c:4885`. UMD reference/written-set limits refuse before the
scheduled callback; they are not evidence of a KMD MaxCount limit.

The actual emitter and OpenCL kernel replay now covers 2400/12000/60000
records, counted and uncounted, seven generators in one modeled batch, all
five IA/VS/clipper counters, exact continuation links, and retained output
allocations. The projected pool allocator is executed for each size and seven
VDM/USC allocations. The production KMD parser/preflight handles the same
combined-envelope shape with one/seven CDM producers. **MaxCount is absent
from that metadata**; its repeated test labels document exclusion of an
alleged envelope-size dependency, not a recording of actual GPU-generated
commands in the kernel parser.

All these cases pass existing encoding/bounds. No record-size, link, USC-window,
timestamp, reference-count or aperture-validation relaxation is justified.
`mesa/winsys/agx_win32_indirect_stream_emit.h:178-190` proves full output
bounds and retains both BOs. Primary Mesa `src/asahi/lib/pool.c:60-70` grows
backing beyond a slab, while `src/gallium/drivers/asahi/agx_state.c:3787-3823`
reserves link/fetch padding. Statistics use five atomics per active record
(`agx_win32_indirect_stream.cl:38-48`); their GPU cost/contention and visibility
remain hardware questions, not established causes of the timeout.

## Confirmed containment gap and bounded fix

Previously, `submit()` uploaded/proved canonical allocations and immediately
called SubmitCommandCb. No corresponding fallible check validated the native
envelope's framing, dependency fields, addresses, lease, geometry or output.
A malformed native envelope could therefore first be rejected inside the
scheduled virtual-submit callback. The regression removes the new call in a
temporary source copy and observes the actual Submit callback-count assertion
fail; it does not merely search for a string.

The new `shared/include/apple_agx_g4_preflight.h` defines an explicit versioned,
4384-byte buffered request. It contains copied metadata, not pointers, fences,
physical addresses or scheduling authorization. C and C++ layout matches for
x86, x64 and ARM64 Windows targets. No new mixed-linkage exported UMD function
is introduced; the UMD helper is static, including under x86 /TP.

`render-admission/umd/src/umd_gpuva_windows.c:2006` calls the escape;
`:2119` gates SubmitCommandCb after upload/native residency proof. On refusal
it records `reject-native-envelope` with status/parse/subsite/kind/ordinal/VA/
bytes/write and returns **0 = unsubmitted**, never 2 = accepted/uncertain.
The existing owner failure path removes the application device. No signal,
render-fence advance, completion or successful draw is fabricated. The existing
winsys Submit=0 branch releases this unsubmitted residency set
(`mesa/winsys/agx_win32_gpuva.c:307-310`).

`render-admission/src/gpuva_g3_windows.c:5084` authenticates process, context-list
membership and device under the existing graph mutex, checks current poisoned/
faulted state, and runs `AdmissionG4PreflightLocked` at `:5025`. That helper
uses the existing render/compute parser, typed access, private geometry and
output proof. Lease identity is checked independently of scheduler-slot
availability. It reserves no scene, fence or backend image. Like R147's
native-allocation query, it accepts a valid process graph before the context's
scheduled SetRoot notification (`:1493`).

WDK26100 `d3dkmddi.h:5797-5821` supplies authenticated escape handles and a
PASSIVE_LEVEL fallible callback, separate from SubmitCommandVirtual at
`:5218-5245`. The PAGE_FAULTED fields at `:1063-1078` require real fault identity;
preflight has no submitted fence and cannot manufacture that notification.

**Scope limit:** the escape is not an immutable submission ticket. Submit and
BeginJob keep all existing validation. Mapping/state may change after preflight;
OS resubmission bypasses it; a caller can bypass this UMD. Runtime/queue/reset
failure, physical/system-paging submit, and unreturned GPU ownership remain
uncontained by this change. A universal guarantee requires an accepted error
packet or retained ticket protocol with capacity, ordering, preemption,
resubmission, reset, lifetime and exactly-once fault notification. Returning
SUCCESS or reusing rejected-fence tails would be wrong. The strong universal
wording in task item3 is therefore **not complete** here.

## New named probes

`windows/icd/agx_d3d12_probe.c:1092,1373,1393,1520,7722` adds:

- **RC57C**: R32 indexed, MaxCount2400/count2400, one stream.
- **RC57U**: R32 indexed, uncounted MaxCount2400, one stream.
- **RC57M**: seven counted MaxCount2400/count2400 streams.

All three open only PIPELINE_STATISTICS around the indirect draws, end it,
then issue an ordinary indexed draw without a VS rebind. The 64KiB upload
contains all 2400 twenty-byte arguments and a nonoverlapping count at50048.
IA/VS counts must be6×indirect draws, IA/clipper primitives2×draws, PS nonzero,
and absent-stage GS/HS/DS/CS counts zero. The independent PS UAV counter must
include the ordinary continuation exactly once. Pixel, counter and statistics
failures all affect the process verdict. Begin/indirect(s)/End/ordinary/Resolve
ordering and perturbations of each counter are executed in host tests.
These named steps do not enter the default probe suite.

## Ownership, verification and next checkpoint

UMD/Asahi owns draw encoding, hazard ordering, BO retention and pre-submit
failure propagation. KMD/VidMm owns mappings, validation, scheduled execution
and terminal accounting. Firmware owns live GPU execution. m1n1/Mu still own
initialization, IRQ, DMA, power and recovery under the saved EXP1978 launch
contract; this offline task changes none of them. No external function body
was copied. Caps, signing, default stream policy and protected invariants are
unchanged. Integration owns experiment/change ledgers.

The handoff REPORT lists final commands, results, commits and source hashes.
Host ASan/UBSan and projected compilation are not native Windows WDK/package
or GPU validation. The extra fallible escape/parser walk per native batch has
unmeasured overhead; compare frame timing as well as correctness.

Hardware owner only: complete native ARM64/x64/x86 /TP compile/link, package
hash/preregistration and recovery preservation first. Run WARP probe semantics,
then stream=0 controls and stream=1 RC43I16Z/O/N and RC43I32Z/O/N, then RC57C,
RC57U, RC57M in fresh processes. Require pixels, _COUNT and _STATS, actual
path3/reason0, and healthy device/DWM; fallback or mere API success is not a
pass. Collect any reject-native-envelope row before further work. Only after
these gates run GravityMark HD50k with the stream and compare render_fence,
frame-time distribution and score with EXP1979's default arm. Stop on a new
fault and collect receipts/logs/dump before the established exact-package
cleanup. No hardware checkpoint was run or preregistered by RC57.

WHAT IS STILL UNKNOWN: exact crashing DDI/predicate, initiating firmware
Timeout and high-count GPU behavior, universal late-submit containment,
native Windows build/link, and performance versus EXP1979.
