# EXP504 internal latch interrupt ownership

WHY THIS HYPOTHESIS: EXP503 actual first ContextCount0/Flags1 source-address
call reached the scanout queue at PASSIVE with every argument/mode/visibility
guard satisfied but IrqEnabled0. Current Start never enables the channel;
ControlInterrupt incorrectly owns both internal completion and OS notification.
The physical broker retains an exact latch status and exposes an interrupt only
when IRQ_ENABLE is set. Therefore notification subscription must not prevent
the initial modeset from completing.

WINDOWS CONTRACT: FULL GRAPHICS, pinned WDDM3.0. SetVidPnSourceAddress modeset
uses ContextCount0 and must remain bounded/nonpageable. DxgkDdiControlInterrupt
is PASSIVE and explicitly permits keeping the interrupt enabled for an internal
purpose. Its Enable flag requests reporting to DxgkCbNotifyInterrupt. The full
graphics portion of Saving Energy with VSync Control describes independent OS
VSync counting; DISPLAY_ONLY optional/simulated-VSync sections do not apply.
No display-only admission/capability assumption is transferred.

AGX/ASAHI CONTRACT: Asahi iomfb_template.c dcpep_cb_swap_complete records the
matching physical swap and dispatches page-flip completion independently of
Windows notification policy. Existing m1n1 display_scanout_latch_poll and
dcp_iomfb_latch.c require the exact D589 swap id; hv_agx_scanout_service.c
separates APPLIED from LATCHED, hv_agx_scanout_broker.c retains status and
matches sequences, and hv_agx_power_mmio.c injects only a pending enabled edge
through the current vGIC route. No platform behavior changes. Mu current
J313AppleAgxAbiAdmission.asl.inc publishes the single edge interrupt889 and
broker MMIO; KMD existing interrupt.c owns mapping/ingress/quiescence.

TRANSLATION: scanout Start owns internal IRQ enable after registered runtime
publication, and Stop retains existing disable. Add a zero-initialized
VsyncNotifyEnabled subscription. ControlInterrupt changes only that subscription;
it must not mask the physical latch needed to retire a queued modeset or flip.
ISR consumes and validates the exact broker sequence, retires PendingValid/gate
once regardless of subscription, and calls NotifyInterrupt/QueueDpc only while
the OS subscription is enabled. Existing source-address guard still requires
internal IrqEnabled. No address/segment/allocation, cap, queue protocol or
render/paging change. No polling/synchronous APPLIED substitution for latch.

ATOMIC CONTRACT: initial internal enable + independent OS subscription +
unconditional exact completion retirement + conditional NotifyInterrupt/DPC.
Enabling alone reports unrequested VSync; deleting the guard alone strands a
pending present while IRQ is masked; filtering ISR entry by subscription loses
physical completions. These are one inseparable ownership mapping, supported by
Microsoft ControlInterrupt remarks permitting interrupts required internally.

WHAT IS STILL UNKNOWN: whether the current physical initial Windows scanout
request reaches and completes DCP latch after this deterministic mapping, and
which next natural Windows primitive follows. This is a hardware verification
of an offline-derived mapping, not capability guessing. No TA3D or rendered
content is assumed from source-address success or physical scanout alone.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Actual production Start tail,
ControlInterrupt and ISR linked with the real shared scanout decoder reproduce
EXP503 internal IRQ0 after Start. The regression fails there before the fix.
It then requires internal enable across subscription FALSE/TRUE/FALSE, exact
pending retirement with no unsolicited notification, matching notified address,
no duplicate notification, and rejection of stale sequence without retiring new
work. Windows atomics and MMIO alone are shims; decoder/state transitions are
production code. Address/undefined-behavior sanitizers enforce memory correctness.

Sources/specs: production scanout_windows.c, interrupt.c, lifecycle.c; shared
apple_agx_scanout.c/.h and apple_agx_fixed_panel.c/.h; Asahi source git HEAD
drivers/gpu/drm/apple/iomfb_template.c swap-complete callback and DRM vblank core;
m1n1 broker/service/power-MMIO/display/D589 parser named above; Mu generated
ACPI source; pinned WDK26100 source-address ABI and official Microsoft
DXGKDDI_CONTROLINTERRUPT / DXGKDDI_SETVIDPNSOURCEADDRESS / VSync control pages.
No external code copied. The Asahi sparse checkout lacks apple files on disk;
git show read the primary objects without changing the checkout.

Checkpoint/recovery: exact504 full-owner477/406 natural bind; expect first
source-address receipt to show IRQ1 and advance beyondC000000D, then inspect
actual first status/ETL/host trace. Preserve evidence and exact package cleanup
before ordinary377/392 restore. No pending completion is declared proven without
positive receipt. Same agent proceeds to the next causal boundary.
