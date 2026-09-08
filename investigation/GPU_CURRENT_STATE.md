# GPU current state

## Active user-priority roadmap — accelerated desktop first

See [ACCELERATED_DESKTOP_ROADMAP.md](ACCELERATED_DESKTOP_ROADMAP.md).
The user requests accelerated Windows/DWM desktop before OpenGL/CS1.6 work;
software/display-only is not the next target. AD01 is OFFLINE_PROVEN and AD02
is HW_PROVEN by EXP651. Current gate is AD03 production composition of the
pinned dynamic triangle graph with the hardware-proven EXP208 backend.
This roadmap pointer changes planning priority only, not hardware readiness or
the last verified machine state recorded below.

Updated 2026-09-09T01:14Z. Main process only; no agents.

## Current machine / next boundary

AD02 is HW_PROVEN by EXP651. Two distinct immutable128-byte allocation-relative
commands passed KMD Render/Patch/Submit and physical AGX execution with exact
fences256/257. Dynamic green full-frame output verified4,096,000 pixels/hash
0xb844371c0d762325; dynamic blue bottom-band verified2,048,000 pixels/hash
0x679fbd632040a325. They used distinct allocations/offsets/PAs and produced exact
query sequences3/4 plus host A408/D589 swap10/11. HOLD15s stayed ACTIVE and
records were byte-exact. Explicit retirement sequence5/fallback swap12 and all
teardown statuses0. `TRANSPORT_HW_PROVEN=YES` and
`DYNAMIC_CLEAR_HW_PROVEN=YES`; D3D pipeline/desktop remain NO.

Exact intermediate package was removed. Ordinary377/392 is clean at
2026-09-08T21:24Z: Code28/null INF, no AppleAgx package/service/module/SYS/UMD,
SSH/8CPU/NVMe2/USB5/keyboard1 and no fresh41/1001/129. Current boundary is AD03:
source-first Mesa compiler/encoder integration for non-replay geometry/shader/
resource workloads. Caps remain0 until AD04 mandatory contract completion.

AD03 Task1 is OFFLINE_PROVEN at commit
ccf17dbd033d1b16fead79b7ce53529a2ed2aba3: exact pinned source contract reuses
only Mesa frontend/compiler/encoder and rejects the softpipe/llvmpipe Windows
target plus DRM/fd owners. Compiler core is OFFLINE_PROVEN at commit
ce5dc1e2912d8485fbc0426335277a8f34d2da39: pinned Mesa builds, identical NIR
input produces byte-exact160-byte AGX binary twice, changed store constant
produces a different binary hash with the same register/scratch layout. Generic
VS/FS output cannot be sent directly to the backend compiler; Asahi driver
tilebuffer/UVS lowering is required. Current first boundary is therefore the
Windows Asahi screen/BO/fence and vertex/fragment lowering adapter, not a
hardware EXP. Upstream `libasahi` failing first at `xf86drm.h` independently
confirms the DRM owner that Task3 must replace. `AGX_COMPILER_CORE_OFFLINE_PROVEN=YES`;
full compiler/encoder and all pipeline readiness remain NO.

AD03 Task3 partial commits3d8ca2df3765d9bc2bd453a48677ec81d1d56dd4 and
36fea56be7e127335b053be36ff75d5f958462f0 add the portable classed screen
contract and supported UMD `pfnQueryAdapterInfoCb`/KMD
`DXGKQAITYPE_UMDRIVERPRIVATE` path. The read-only response reports G13G,16K
pages, boot generation and bounded logical classes without raw VA; context
generation remains separately owned by CreateContext. FullProduction ARM64
Universal/sign build and real x64 UMD callback mock pass. Commit
3aa5741c4e605051b9804749c121bc9a034680cd now wires class buffers to real
internal WDDM Allocate/Lock/Unlock/Deallocate callbacks with opaque bounded
tokens and exact teardown; portable tests real x64 callback mock and ARM64
WDK26100 Universal/sign build pass. Broker VA/typed relocation and Windows
fence waits were the next boundary. Commit
f396856ce4af0bd612b793a6b283ee4776a5ab9b now uses bounded Windows-context
`EnqueueCpuEvent` completion tokens with exact timeout rollback retire and
teardown semantics instead of Linux syncobj or receipt polling; real WDK
callback tests and ARM64 Universal/sign build pass. Actual Mesa `pipe_screen`
construction was the next boundary. Commit
c574d40520befed878050a2f20a4131b758f936d now constructs real Mesa
`pipe_screen`/`pipe_context` objects with conservative zero caps bounded
buffer/BGRA8 resources and checked transfers over the Windows screen; exact
pinned-header clang sanitizers and MSVC14.44 analysis execution pass, with no
DRM/fd/software target. Production ARM64 UMD linkage plus vertex/fragment
lowering and typed draw graph submission were blocked first by missing KMD
class identity. Commit29aa0281c7f0d3c93203bfc9e3ce9e9e61f13dc4 adds a
versioned internal allocation descriptor and preserves General/Shader/Encoder
plus exact access through UMD Allocate and KMD Create/Open/Render facts; RED to
GREEN parser tests real callback mock and ARM64 Universal/sign build pass.
Typed Draw graph validation and role-to-class relocation policy are now the
current first boundary. Commit
c9c20e507fa84440a31c59f9315b4373a2549035 now implements a versioned
pointer-free Draw graph, immutable UMD builder, class/alias/range/reachability
validation and rollback-safe copy-once encoder/pipeline materialization with
callback-resolved40-bit VAs. Malformed and mutation tests are GREEN; the module
is compiled into ARM64 KMD build659. `DYNAMIC_JOB_ABI_OFFLINE_PROVEN=YES`, but
production physical-range reader/resolver and backend publication remain
absent, so validated Draw returns STATUS_NOT_SUPPORTED and cannot reach a
queue. That production composition with the pinned Mesa encoder is the current
first boundary. No hardware candidate is justified and pipeline caps remain0.

AD03 graphics-stage compiler is OFFLINE_PROVEN at commit
0aa10b4d35cbdb88554873434d318f4c35aaa632: exact pinned Asahi VS
input/prolog+UVS and FS output-to-epilog+sample-mask lowerings produce
deterministic source-sensitive binaries and both main programs pass the pinned
AGX disassembler. `AGX_COMPILER_OFFLINE_PROVEN=YES`; USC pipeline VDM/render
pass encoder serialization and production Draw publication remain the first
boundary. This is not hardware or desktop evidence.

AD03 encoder progressed through commit
5e98915c78067181462123857e52b3c2ab9b4901. The pinned fixture now compiles a
vertex-ID triangle, links the fragment main with the Asahi BGRA8 tilebuffer
epilog, serializes92-byte USC pipelines plus a complete228-byte VDM/PPP stream
(`VDM state -> PPP state -> triangle draw -> terminate`), and emits exact
scissor/depth arrays. Seven typed relocations include both shader rodata
addresses and the split40-bit PPP self-pointer; generated unpack, disassembly,
ASan/UBSan tests and WDK26100 build661 are GREEN. The render-pass store/EOT
pipeline remains explicitly owned by the hardware-proven EXP208 3D skeleton;
it is not duplicated. `AGX_COMPILER_OFFLINE_PROVEN=YES` and
`DYNAMIC_JOB_ABI_OFFLINE_PROVEN=YES`; dynamic physical Draw remains NO.
Current first boundary is a transaction-owned production overlay that copies
these validated objects into the unused regions of EXP208 objects71/73/74,
preserves fixed store-pipeline data, resolves exact original shader-base VAs,
and carries the immutable result through Render/Submit/worker. No Air package
was staged and pipeline caps remain0.

EXP649 is rejected at one source-exact post-output presentation guard, not at
the Win32 transport or AGX backend. Its first128-byte full-green command passed
Render/Patch/Submit, physical completion fence256, DPC and exact terminal output
verification. `AdmissionScanoutPresentAgxResult` then exited
`0xc0000206 STATUS_INVALID_BUFFER_SIZE`, no query record was published, and
frame2 correctly did not start. The sole corresponding source return is the old
`AdmissionVisibleAgxUseFramebuffer` fixed-colour classifier. Commit
e22ebc160c3a3a91d6c9393766d4c41f5e6631dc removes only that stale classifier;
full-size/hash/prefix and all owner/range/active/latch guards remain. Offline
RED→GREEN and adjacent tests pass. EXP650 is preregistered to repeat the same
two dynamic commands with this one variable.

EXP649 exact oem5 was removed. Ordinary377/392 is clean and verified at
2026-09-08T21:04Z: APPL0002 Code28/null INF, no package/service/module/SYS/UMD,
SSH,8CPU,NVMe2,USB5,keyboard1 and no fresh41/1001/129. AD02 is not yet
HW_PROVEN; pipeline caps remain0.

EXP650 hardware-confirms the dynamic-colour scanout fix for frame1: full green
has physical fence256,4,096,000 verified pixels, hash
0xb844371c0d762325, query sequence3 and exact A408/D589. Frame2 also reaches
physical fence257, output verification and a second D589, but its query guard
returns STATUS_DATA_ERROR. The exact defect is allocation-base terminal PA
being compared directly to rendered-band PA (base+8,192,000). Commit
d88cde2216ef600f6ef18b80a3c75c96e088362b validates base/capacity and exact
rendered CPU/GPU/PA offset instead. EXP651 is preregistered with only this
variable. EXP650 package is removed and ordinary377/392 is clean at21:15Z.

EXP648 definitively names the D3D11 admission boundary. The unchanged probe
loaded and executed exact UMD30.0.648.0 in the Apple-adapter process. Correlated
DBWIN records for probe PID5456 show OpenAdapter10_2, GetCaps pipeline0 and
GetSupportedVersions; CreateDevice is never called. D3D11CreateDevice returns
DXGI_ERROR_UNSUPPORTED while Basic Render and WARP both create feature-level11_0
devices. Thus the runtime rejects the truthful zero-pipeline contract before
UMD CreateDevice. This also definitively corrects EXP646: post-return module
absence meant queried then unloaded, not never loaded.

Exact oem5 package was removed after evidence. Ordinary377/392 is restored and
verified at 2026-09-08T19:08Z: SSH, one inert APPL0002 Code28/null INF,
packages/service/module/SYS/UMD absent,8CPU,NVMe2,USB5,keyboard1 and no fresh
41/1001/129. Current SSH ED25519 key matches the previously pinned project-local
key; the global known_hosts file was not changed.

Current first unknown remains a coherent accelerated Windows frontend plus
standard Windows presentation. EXP641 proved
only that an SSH session0 producer cannot acquire exclusive VidPN source0.
EXP643 proved one interactive Windows render and physical AGX completion
fence269, but windowed BLT Present was denied before the KMD DDI. EXP644
advanced the denial to OCCLUDED while DWM reported
MILERR_DEVICE_CREATION_FAILURE. EXP646 proves D3D11CreateDevice(Apple) returns
DXGI_ERROR_UNSUPPORTED while Basic/WARP pass because the exact installed Apple
UMD has no truthful 3D pipeline/device contract. EXP647 now rejects the
independent fullscreen direct-KMT seam even from a real console token. Current
first unknown is therefore a coherent minimal truthful UMD device/pipeline and
runtime-managed presentation contract; no capability bit may be enabled alone.
Caps remain unchanged. Commitfac65c79f4b179219b6afaf3bad64d0064b0049e fixes
format-query error semantics and exact allocation association/primary release/
deferred Flush lifetime with pipeline caps still zero. EXP648 closes the cold
loader/callback question; do not run another callback or capability probe.
AD01 source/build gate is closed. Commit e8007fac4630fa1fab82cbbe591d6e2d3a2c9b28
selects Mesa D3D10 reuse at feature level10_0 and records121 mandatory adapter,
device, DXGI, semantic, transport and sharing rows. All current missing rows are
false and the advertised pipeline mask remains0. Commit
f9ff9b92f9124db6777638975661e213efba2289 replaces silent terminal resource
Abandon with explicit attempted/deallocated/undeallocated accounting and raw
errors. Commit dfdd31ef615ef6b3a2ff57c566b6ae5fc88e779c makes DBWIN capture
ready/stop, PID/token and loss-aware. x64 real mock and ARM64 code-analysis
builds are GREEN. Current executable boundary is AD02 Task1: versioned,
allocation-relative, copy-once command envelope. A truthful nonzero pipeline is
still forbidden until all mandatory rows are implemented/tested. DWM/standard
Present remains an explicit later gate; WARP control success does not prove it.

## Hardware proof retained

- EXP475/477/478: retained-root/context0/RTKit/firmware/native initdata,
  BackendRuntimeStart, arena/context/queues.
- EXP581/585/586/588: Windows-originated physical TA/3D, exact output,
  completion and Windows fences. Completion ingress remains polling.
- EXP591: physical panel scanout photo.
- EXP631/632: two full2560x1600 outputs/latches,15s HOLD, owned-primary
  retirement and all teardown statuses0.
- EXP634: four exact repeated full frames and latches.
- EXP636/638: kernel dumps prove recurring0x101 CPU4 during uninterrupted
  output byte hash over mapped DXGK noncached physical memory at IRQL0 with
  guest IRQ mask clear. Aggregate monitor telemetry did not establish a
  timer/vGIC fault.
- EXP637: same verifier over user pagefile-backed noncached memory, fixed CPU4,
  sixteen9.4s scans PASS.
- EXP639: dedicated system thread satisfies Microsoft's work-item contract but
  does not eliminate0x101; shared worker pool hypothesis rejected.
- EXP640: sixteen Windows Render/Patch/Submit physical TA+3D operations,
  exact completions/fences256–271, sixteen full4096000-pixel results with
  alternating hashes, sequences3–18 and16 A408/D589 latches. Two exact owners
  reuse offsetsfa0000/1f40000 and PAs9bcf90000/9bdf30000. No stale/duplicate
  completion, active-surface write or corruption.
  - HOLD15s: all16 records byte-exact, device ACTIVE, Code0/service Running,
    8CPU/NVMe2/USB5/keyboard1, no fresh41/1001/129.
  - Retirement: retained Windows primary sequence19 ownerffffd3817b01f670
    offset0/PA9bbff0000, exact D589; allocations/contexts/paging queue/device/
    adapter all destroy/close0; producer result0; final uptime205s.
  - Physical repeated color observation remains pending/uninstrumented.
  - This is direct full-frame AGX output plus private qualification presentation,
    not standard Windows Present/DWM/OpenGL.
- EXP641: exact standard-flip candidate bound Code0, but exclusive VidPN source
  acquisition returned0xC01E0342 before Render/Present. All WDDM objects
  teardown0 and clean ordinary recovery PASS. Exclusive session0 route is
  rejected; no standard-present hardware readiness bit changes.
- EXP642: 0x7E c0000005 at AdmissionDdiPresent line128 before scheduled task;
  wrong diagnostic union view read handle0x2. Windowed BLT producer remains
  untested. Exact cleanup and ordinary recovery PASS.

## Validated architecture/change

Commitb17e53a5fe7ae2b52481117080be7c559e79a5b4 gives long output work a
per-runtime driver-owned PASSIVE thread with explicit wake/drain/stop/exit and
handle close. Commit6a7e3aa4c976c7cc66077aa2099a77f27b18208f bounds mapped
noncached output reads to256KiB and performs1ms nonalertable progress waits.
The verifier keeps full pixel/poison/guard/FNV results and publishes atomically.
AGX, firmware, queues, fences, DCP and allocation/display ownership unchanged.

EXP640 exact30.0.640.0 hashes:
ZIP2de1a31d762773134824b40e48554d093b935d2ece6dc4f9095f28444f96113f;
SYS2f2cbcf88ecefb5eed4af365a50d84eb50b9ab9a04f0311505d8100ccb33ac67;
INFba16ad7e204f1269cb0a4503065ca1606e80feb6d10a37441bb7985eb701a13b;
CATcc0a6a3dbf6a432122de76c25bae82f68e643248fa8a51e2788692ad3a4b2f82;
UMDcc7485f12480773541eee6f83a21c0af0cacb5b7519d2f5c9b25fed7aade6638;
producerac0b236e9d8571fe0a836c76a696eed04b383403842f58ccc2721227d8929af5.
Final stdout SHA256c06ed83ad7574d421f9f01ffd7f4a1477975d4b79eee6ff58ba296d8440c2bf7.
Evidence: .local/experiments/EXP640-bounded-output.

## Constraints / final goal

Do not reopen retained-root, AGX/PBE/UAT/RTKit/completion/DCP/qualification
lifetime without contradictory evidence. Preserve unrelated dirty tree and
native-ANS. Event129 remains telemetry without causal proof. One causal
variable per experiment; source-first WDK translation. Final mission remains
normal Windows rendering/present, accelerated OpenGL and CS1.6 through real
AGX. Leave only the final accepted package installed; intermediate packages
are removed after evidence.
