# EXP923: capture the exact fullscreen-creation wait

WHY THIS HYPOTHESIS: EXP922 accepted the SDK's FL10 device and both exact BGRA
backbuffers, but CreateSwapChainForHwnd did not return before task termination.
No native mode/Present call or SDK clear occurred. The later ETL has no SDK PID
records, so it cannot identify the blocking call. A repeat without an early stack
snapshot would not distinguish a new cause.

WINDOWS CONTRACT: FULL GRAPHICS API workload remains exact e1d5875c/920package.
An external process invokes documented MiniDumpWriteDump on only that exact owned
SDK process, holding its handle and checking image hash/session/non-critical state.
MiniDumpNormal plus MiniDumpWithThreadInfo captures thread stacks; no full-memory
or token dump, no process termination. One call, no overwrite or automatic retry.
Microsoft specifies an external dumper to avoid in-process loader deadlock:
https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump
and the type definition:
https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/ne-minidumpapiset-minidump_type.
AGX/ASAHI CONTRACT: no driver, firmware, resource-layout, UAT, IRQ, queue or capability
change. Native presentation resource source is explicitly AIL_TILING_LINEAR with
DRM_FORMAT_MOD_LINEAR; no blind detile/pitch change based on photos.
TRANSLATION: prepare an external observer before starting the SDK process so a
later Win32/SSH stall cannot prevent the snapshot. Once the exact SDK PID has been
published, capture its stacks at five seconds while still in fullscreen creation,
before the existing sixty-second task bound. Preserve early ETL/UMD/frame data.
WHAT IS STILL UNKNOWN: the exact runtime/kernel wait after backbuffer creation;
the writer of the observed corrupted physical pixels; whether a primary/display,
fence, Win32 or frontend contract is violated. No source fix is selected yet.

Inspect public Microsoft symbols on the builder with the exact snapshot and native
image/PDB hashes. If the stack distinguishes a cause, compare that owning source
with the supported contract and reproduce any deterministic defect before fixing.
Do not advertise a new DDI or normalize NO_REDIRECTION from this observation alone.
One instrumentation variable; no repeated DWM action. Collect immediately and
use ordinary377/392 Code43 exactrollback then Code28twice. Every artifact/launch
must be preregistered and hash-gated; this plan alone is not a hardware launch.

Observer implementation: compile DbgHelp PInvoke before readiness; publish exact PID
and process start time atomically; hold the matching handle and capture once at five
seconds. Immediately stop the experiment-owned EXP801DxgBoot logger and copy its
ETL plus UMD trace, preserving early evidence before the task deadline. This is the
single snapshot-instrumentation variable; SDK binary/package/firmware stay exact.
Original collector may accept the already-stopped logger only with matching-boot
early receipt and every recorded early file size/SHA verified. No fake regression
test is added: this is diagnostic instrumentation, not a confirmed driver defect.
