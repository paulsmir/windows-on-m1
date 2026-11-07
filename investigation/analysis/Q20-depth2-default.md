# Q20: accepted depth-two configuration as the default

The change promotes the configuration accepted by EXP2096–2099,
EXP2102/2107/2108 and EXP2109. It changes startup option resolution only.
The scheduler, firmware barriers, manager/flight lifetimes, completion order,
recovery, capability advertisement, firmware, shader/compiler code and signer
are unchanged. No hardware or guest was accessed for this task.

## Evidence and contract

Paths below are relative to `drivers/apple-agx/` unless prefixed otherwise.

- `investigation/EXPERIMENTS.md:67519` and `:67545`: ordinary R/R and C/R
  admission with exact retirement and no resets. EXP2098/2099 subsequently
  validate both synthetic two-flight TDR terminal orders.
- `investigation/EXPERIMENTS.md:67614` records EXP2100's firmware crash;
  EXP2102 succeeds with prelude-bearing pairs refused. This change preserves
  that gate, not a claim that the prelude topology is repaired.
- `investigation/EXPERIMENTS.md:67717`, `:67739`, `:67761`: diagnostics-off
  depth one, depth two, and then the sixteen-entry UMD window. EXP2108 scores
  5771 HD / 4665 FHD on GravityMark D3D12 100k, temporal=0.
- `investigation/EXPERIMENTS.md:67781`: EXP2109 has 119 PASS, zero FAIL and one
  existing C21E SKIP. The decision at `:67787` authorizes promotion and records
  that real GPU fault/hang recovery remains a pre-existing limitation.
- `render-admission/src/backend_platform_windows.c:6833`, `:6890`: the existing
  registry readers accept exact REG_DWORD values with a four-byte payload.
  `SwitchDisabled` is true only for zero; absent, unreadable, wrong-type,
  wrong-size and other DWORD values use the feature default. Both helpers
  close an opened registry handle.
- `render-admission/src/backend_platform_windows.c:7261`: NotifierFlight is
  configured before provider initialization. `:7309–7381` resolves the other
  options before backend start, retains the dependency gates, propagates
  manager depth and updates the provider's retained AbortFaultSuffix setting.
- `render-admission/src/render_job_window.c:74–83`: General's accepted class
  mask is 14 (TA-on-compute, TA-on-3D, compute-on-3D). C/C adds bit 16 only
  through its explicit opt-in. `:85–112` preserves fault, reset, preemption,
  pending-fence, process, paging and prelude checks; subsequent checks preserve
  full live-manager identity, initialization and word-set exclusivity.
- `mesa/winsys/agx_d3d12_windows.cpp:478–491`, `:889`: the D3D12 owner samples
  the environment once after initialization and sets both its winsys window
  and its runtime allocation-hold limit together. The common unconfigured
  fallback remains four; D3D11/GL do not call this D3D12 configuration function.

Primary reference sources inspected, without copying their implementation:
`.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/queue/mod.rs:284–330`
(notifier threshold advances only at publication), `:787–820` (typed barriers
on predecessor firmware stamps), and `buffer.rs:684–722` (live manager slot and
usage ownership). Local WDK 26100 `d3dkmddi.h:1009–1021` requires original
submission/preemption fence identities; local SDK/WDK `km/wdm.h:38286`, `:45753`
declares the registry APIs used by the existing readers. No new Windows ABI
or hardware protocol is introduced.

KMD owns configuration, publication, DMA and manager/flight lifetime, interrupts,
completion and recovery. UMD owns queued submission sets and allocation holds;
VidMm owns placement and residency. Firmware executes the existing barriers.
Asahi's DRM timeline is represented by the existing Windows completion path.
The pinned m1n1/Mu gitlinks are uninitialized here; inherited state, power,
address mappings, ACPI and the accepted launch/recovery contract are unchanged.
Saved experiment evidence is the machine reference under the offline restriction.

## Switch table

All KMD entries are per-device registry DWORDs read at startup. For default-on
entries only an exact DWORD `0` disables; `1` and absence select the accepted
value. For default-off entries only an exact DWORD `1` enables. Prerequisites
remain mandatory. These settings require a fresh driver start; the UMD setting
requires a fresh D3D12 owner/process.

| Setting | Absent | Explicit 0 / opt-out effect |
|---|---|---|
| `Wom1ManagerSlots` | 1 | Single manager slot; General disabled; existing R/C-only fallback may remain |
| `Wom1JobBuildAhead` | 1 (unchanged) | No build-ahead, depth-two capabilities or kicks |
| `Wom1JobDepth2` | 1 | Depth one; build-ahead retained |
| `Wom1JobDepth2Caps` | 1 | No second-sidecar/barrier capability mask; no early kicks or General |
| `Wom1NotifierFlight` | 1 | Previous notifier path; Rc/General kicks disabled |
| `Wom1RcDepth2` | 1 | Removes legacy R/C-only policy; no effect while General is effective |
| `Wom1Depth2General` | 1 | Legacy admission; with other defaults retained, RcDepth2 limits kicks to R→C |
| `Wom1Depth2TaOn3d` | 1 | General mask excludes TA-on-3D / ordinary R→R |
| `Wom1Depth2TaOnCompute` | 1 | General mask excludes TA-on-compute / ordinary C→R |
| `Wom1Depth2Prelude` | 0 (unchanged) | Either prelude endpoint remains refused in General mode |
| `Wom1Depth2ComputeOnCompute` | 0 (unchanged) | General mask excludes C→C |
| `Wom1RcReceipt` | 0 (unchanged) | Typed diagnostic sampling/export remains off |
| `Wom1JobDiagnostics` | 0 (unchanged) | Optional job timing/window diagnostics remain off |
| `Wom1JobDepth2Kick` | 0 (unchanged) | No legacy broad-kick opt-in; Rc/General have independent kick controls |
| `Wom1JobDepth2KickOn3d` | 0 (unchanged) | No legacy broad 3D class opt-in; cannot widen General's mask |
| `Wom1ManagerSaveOnEvict` | 0 (unchanged) | No standalone serial-persistence opt-in; General still requires persistence |
| `APPLE_AGX_D3D12_SUBMISSION_WINDOW` (environment) | 16 entries | Exact string `0` restores 4 entries in both D3D12 owners |

The environment variable remains a Boolean selector, not a numeric window size.
An empty string, `1`, or any other nonzero/malformed string selects sixteen;
`00`, `0 ` and ` 0` do not match the explicit zero opt-out. No shared maximum,
allocation size, invalid-window fallback or other frontend default is changed.

For the EXP2106-style depth-one comparison set `Wom1JobDepth2=0`; also set the
UMD variable to `0` to restore four-entry D3D12 tracking. To recreate the entire
pre-Q20 absent-value policy, additionally set ManagerSlots, JobDepth2Caps,
NotifierFlight, RcDepth2, Depth2General, Depth2TaOn3d and Depth2TaOnCompute to
DWORD zero; leave JobBuildAhead enabled, as it already was. Individual opt-outs
are independent controls, not promises to disable every related feature.

### Why RcDepth2 stays

`backend_platform_windows.c:7344–7348` installs the legacy R/C kick policy.
General overrides it at `:7355–7363`; the worker bypasses the Rc pair restriction
only when General is effective (`:5543–5545`).
`render-admission/include/render_rc_progress.h:178–181` otherwise restricts the
pair to ordinary render→compute. Thus RcDepth2 is unnecessary to set explicitly
in the accepted configuration but is not globally inert. Removing it would
change General=0 behavior and compatibility with legacy kick selectors.

## Regression proof and changed historical assumptions

`tests/test_depth2_defaults.py` compiles the actual registry readers and startup
assignment blocks with the real window/manager headers and real capability
helpers. It checks every default-on entry with absent, zero and one; each
retained default-off entry with absent, zero and one; malformed/unreadable
values; exact prerequisite effects; and build-ahead compiled out. Expected
configuration is asserted independently as a complete field tuple, including
provider fault-suffix and manager persistence state. The registry stub models
only IO; it does not replace production option parsing. Compilation uses strict
warnings, ASan/UBSan and pattern stack initialization with WDK-width ULONG.

RED on the base: 130 assertion failures in nine tests, including absent KMD
policy and absent UMD window. GREEN after the default changes: 23 tests pass.
The broader targeted admission, manager, notifier, recovery, diagnostic,
submission-window, residency and KMD-header suite passes 95 tests.

Only these historical assumptions changed:

1. `test_render_job_depth2_receipt` now expects default-on resolution for
   JobDepth2; its build-ahead and compile-time gates and passive sampler tests
   are unchanged.
2. `test_render_job_depth2_caps_rc98` now expects default-on resolution for
   JobDepth2Caps; all capability dependencies and decision/builder tests remain.
3. `test_depth2_general_admission` now represents a cleared legacy fixture mask
   bit as an explicit DWORD zero, preserving all original behavioral assertions.
   The new production-reader test separately exercises genuine absence.
4. `test_d3d12_submission_window` now expects exact zero→four and otherwise
   sixteen in both owners. Raw zero/unconfigured winsys and runtime tests still
   require four, and their retirement, failed-wait and hold tests are unchanged.

Offline Windows-target Clang compiled the changed D3D12 owner to COFF for
x86/x64/ARM64 and checked the full KMD translation unit with pinned headers and
warnings as errors (WDK headers marked system; Clang compatibility suppression
for the WDK ARM64 token-paste extension). This is not native WDK/MSVC `/W4 /WX`
compile/link evidence. Exact commands, output and object hashes are retained in
the Q20 handoff. The required full baseline printed:

```text
ran 836 tests in 333 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

The new KMD module is outside that runner's default name patterns and is covered
by the 95-test targeted command. No baseline exemptions changed. Final commit
identities are in REPORT.md.

## Integration validation and remaining unknowns

WHY THIS HYPOTHESIS:
1. EXP2096–2099 and EXP2102/2107 validate the accepted ordinary pair classes and
   synthetic recovery, with prelude pairs refused.
2. EXP2108 validates the sixteen-entry window, and EXP2109 validates the complete
   configuration with 119 passing D3D12 steps.
3. This change resolves absent values to that same explicit configuration;
   executable tests show the equivalence and preserve opt-outs.

After native WDK `/W4 /WX` and x86/x64/ARM64 UMD compile/link, the integration
owner should hash and preregister source, package/manifest, m1n1/Mu and known-good
GPU-visible recovery. The smallest hardware checkpoint is one run with **no
feature overrides and no UMD submission-window environment variable**, leaving
only `Wom1GpuFaultInfoRead=1`. Expect the full D3D12 suite to retain **119 PASS,
0 FAIL, one pre-existing C21E SKIP**, then GravityMark D3D12 100k temporal=0 HD
and FHD near **5771 / 4665**, correct output and normal exit, no reset/G4 failure,
and a healthy desktop. Normal diagnostics should remain off. An unexpected
reset, failure, corruption or score regression beyond noise fails equivalence;
missing diagnostics alone cannot prove overlap or performance.

Collect outputs, exit codes, reset/failure/WER evidence and dumps before exact
package/devnode cleanup using the accepted GPU-visible recovery to Code28;
GPU-hidden recovery remains emergency-only. The validation is proposed, not run.

WHAT IS STILL UNKNOWN: no-switch hardware equivalence and long desktop soak;
native MSVC build/link on the three UMD ABIs and WDK `/W4 /WX`; real GPU fault
recovery (the final DECISION identifies an existing unrecoverable class);
prelude and compute-on-compute validity. Nothing here claims to repair or
validate those two topologies or real-hang recovery.
