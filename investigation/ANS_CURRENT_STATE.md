# ANS current state — OFFLINE READY FOR ANS001; WAIT FOR USER

Updated 2026-09-06T18:49:04.331094+00:00. No Air permission has been given.
No ANS Air SSH/USB/proxy access, install, launch, reboot or hardware experiment
occurred. Only separate FRYZZING builds/read-only tooling and disposable local
QEMU software VMs were used. All software VMs have stopped.

## Scope and isolation

User requested the supplied native ANS plan, all feasible offline work first,
then permission before MacBook Air tests, with no conflict with parallel GPU
work. Latest question asks when ready/how first test works/whether Windows
boots through this driver. Do not confuse it with permission to run hardware.

ANS root /Users/pavel/public_windows/.worktrees/ans-native, branch
feature/j313-ans-native-offline. Independent root/m1n1_windows/mu/MU_BASECORE
worktrees. Original /Users/pavel/public_windows is GPU work: never edit, reset,
stash, checkout or commit there; no shared build/env/launcher changes. Original
GPU runner was detected through read-only host process metadata and left alone.
No merge/push authorized. Before any next turn after reset, read original
investigation/GPU_CURRENT_STATE.md first, then this file; treat it as saved
context, not a fresh hardware observation. Do not message/interrupt GPU task
without corresponding user authorization/coordination.

## Final reviewed packet

.local/ans-native/ANS001-ready/qualification.json SHA256
466036d0f86d9e3a6adff573b25ce1253bf1907811910f95c952f7bf777aecaf.
Manifest, audit, images, build/source/test receipts, five driver ZIPs and older
recovery copies are read-only. COMMANDS.json stores actual successful dry-run
argv and a separate future command requiring --confirm-experiment ANS001.
READINESS.json explicitly says hardware_permission=false, hardware_tested=false,
full_v1_dma_ready=false and native_windows_boot_ready=false. Both validator and
actual run-assisted --dry-run passed without port discovery/chainload/UART.
Independent final review found no blocking issue within ANS001-only scope.

Main hashes:
- m1n1.macho29e8897cb1daf8755f153e1c1fc9900a0464401f000833ddd847797c2bdb3acb
  ANSBUILD004C, clean tag/commit2d869eb.
- native Mu FD85157f29fd43f562559e45dd15c42549befe8f54f20f95d3d5b79db221c880d5
  ANSBUILD005-N1, actual native gate/AutoBoot/compiled AML+FFS proof.
- WinPE D image3db37261578a0fb2793e980f5d0e737adc7d7bab2eb1529e9cf0ec70ce29e8da
  WIM044bfc1ac9ab27d7539cff089c2ace1b61c903b5ab76cd665cacc09a00e80220.

ANS001 is package-free WinPE from preloaded RAMdisk. APPL0003 must appear
unbound/Code28; virtual PCI NVMe absent and internal disk inaccessible. It does
not boot installed C: through AppleAns. Handoff freezes/drains/flushes/stops
inherited ANS and hides recovery; it is NOT hardware-inert. Mu validates a full
EBS receipt, but the raw MMIO receipt is not exported by the current collector;
WinPE reaching its checkpoint is an inference through the enforced code path.
Instructions documentation/ANS_FIRST_TEST_RU.md and packet README.md.

## Critical limitation — do not enable native DMA by assertion

Actual assisted descriptor has DART_TABLES=UNOBSERVED. Strict memory rejection
is correct; an opened-DART tracker misses inherited/future owners. No flags were
cleared, no zero range or completeness was fabricated. Commit2d869eb separates
proven recovery stop/retire/hide from independent memory admission. ANS001 and
logical QUERY/CLAIM can proceed; native controller boot, ANS003/004, Identify,
READ and full V1 remain DENIED. See m1n1_windows/docs/ans-native-memory-admission.md.
Actual runner descriptor -> production C publish/capture -> platform regression
proves successful unbound handoff while boot/translation remain blocked.

## Completed verification

- Windows source07 SHA3f1c5832955519f250733dfbbdb160b5479b7ef18009e019456783d31f1af164
  matches38 committed source files. Five ANSBUILD003 profiles pass real WDK26100
  ARM64/link,10analysisfiles/zero defects each, InfVerif/catalog/test signing.
- ANSVERIFY002:20 test-root Authenticode/member checks pass; pretrust fails as
  expected; all20 Microsoft production /kp checks fail. No driver was loaded.
- Full m1n1 C Mach-O/raw/ELF build passes, source unchanged and clean tag checked.
- Both Mu R1/N1 FDs pass97PE/COFF checks each, source26644files unchanged;
  exactlyone inert APPL0002 both, APPL0003 onlynative, native/cache/gate bindings.
  Same145warning lines each, no native-only warning. No hardware-working claim.
- Current aggregate runner:96Python cases plus complete ANS/common/Windows/Mu
  sanitizer and actual-function suites, seven hypervisor checks. Immutable
  .local/ans-native/tests/receipts/e2be12bc257ebc361dde7883048c5cb44819b71f5389702b05ceb440f9338b69.json.
- Private root proxyenv: Python3.13.12, construct2.10.70, pyserial3.5; ownpackages,
  no originalenv link/globalinstall.83runtime/launcher tests and15safe imports
  passed. Do not import m1n1.setup or construct device interfaces before approval.
- New startup/collector exact bytes tracked in tools/ans_winpe (CRLF preserved).

## Collector qualification and preserved failures

ANSCOLLECT001 failed missing findstr; C/D use CMD built-ins. ANSCOLLECT002/003
failed export with unavailable partition-volume focus; all old evidence stays.
Fourth software run ANSCOLLECT004 on D exported73RAMfiles after exact diskID,
partition-volume-backing/marker checks; core SAN3/wpeinit/PnP/package/loaded/
registry gates passed. Volume already existed BEFORE one rescan; do not claim
rescan necessity or explain old transient from this result.

Mandatory-scope PASS receipt
.local/ans-native/ans001-collector-vm-004/ANSCOLLECT004-mandatory-result.json SHA
7530fe81cfcbfe961f9aec3adf5a344d7a9bdc7ea259329a0aa1b6d83d152e0c.
Earlier strict FAIL retained/linked. Raw optional sc127, DISM0x80004002 and
WMIC0x8007007e remain; all_commands_succeeded=false. Final audit explicitly
classifies WMIC supplementary, while frozen OPTIONAL-CHECKS.CSV lists sc/DISM.
Do not rewrite those errors or claim WMIC success.

## Host launch safety and operator prerequisites

Shared inherited lock covers discovery/bootstrap/chainload/runner in both ANS
recovery/native paths. Legacy run-assisted/chainload/run_uefi processes in other
roots block launch; none is killed. Native exact chainload precedes owned UART
reader and guest bootstrap. Reader has bounded owned-child cleanup, flush/reap
before unlock and parent-liveness exit; original GPU environment is untouched.
Output aliases/hardlinks to sealed inputs and unsafe native overrides rejected.
General native_launch_enabled remains false; manifest is evidence, not permission.

User was asked asynchronously only whether USB flash/hub is available for logs;
no answer received yet. It is not a hardware-approval request. Before actual run:
1. Obtain explicit user approval for ANS001 and exclusive Air slot.
2. Fresh bounded Windows SSH + proxy/vUART/launcher checks; no physical action
   request if machine already usable/Running proxy. Never interrupt active GPU
   launcher as a shortcut.
3. Seal currently compatible GPU rollback pair/state, not assume older377/392
   fits whatever GPU package is then installed. No GPU package cleanup here.
4. Inventory/approve exact external evidence device and usable WinPE input;
   never guess disk/volume/letter or use the VM's invented ID as hardware ID.
5. Append full ANS001 hardware preregistration (WHY THIS HYPOTHESIS, hashes,
   single mode variable, commands, evidence, timeout/recovery) before launch.
   One run only; save evidence before recovery; unsafe stop needs a safe reset
   path, never blind reuse of uncertain DMA state.

Preserved old ordinary recovery:377m1n1 fae3444c... and392Mu16c17718... plus474
health proof, full hashes investigation/ans/RECOVERY_BASELINE.json. It is old
proof, not fresh Air health or universal rollback for future GPU packages.

## Durable history

Source commits: m1n1 2209942 integration +2d869eb correction; MU_BASECORE636ae5d46b;
Mu172217b078; root9f54f2a driver,1a19136 contracts,9389a1d launch,
ba7200f immutable receipts/collector. Full40chars in CHANGES.csv.
All new rows implemented, not validated. Old global CSV has10pre-existing
unsupported change_type values; do not rewrite GPU history merely to pass it.
ANS rows pass existing schema. Initial mutable receipt was lost through later
runs: original row explicitly superseded, old hash retained, no reconstruction.
Launch row's still-available receipt moved byte-identically to immutable archive.
Old draft ANS001-candidate contains4B/B, is not admitted and must not be used.
All agents now finished; root owns files/ledger. No unfinished exec/VM work.
