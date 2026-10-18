# EXP929: per-process private preparation without global adapter drain

WHY THIS HYPOTHESIS:
1. EXP928-LIVEPAIR captured SDK waiting in NtAlpcConnectPort and DWM LPC inside UMD private_escape at the same checkpoint. Matched928 locals prove ACQUIRE operation1, 1024x1024, utile32x32, zero manager/scene handles. The earlier post-test DWM idle snapshot did not localize this path.
2. Private preparation unconditionally requests HardwareAccess1, forcing global scheduler/adapter idle for every native batch, while KMD already serializes its state, waits for the owning process job/lease, and m1n1 independently refuses publication for an owner with live jobs. This makes excessive global synchronization causally closer than pitch/capability/lifetime changes.
3. Real KMD/broker lifecycle replay rejects software-entry ACQUIRE before changes in both16/64 page profiles. The selected-primary lifetime experiment did not observe its destruction; primary import is explicitly linear. Neither finding supports moving the heap or detiling.

WINDOWS CONTRACT:
FULL GRAPHICS. Microsoft D3DDDI_ESCAPEFLAGS defines HardwareAccess as requiring LevelTwo synchronization, not merely as a description of any register access. LevelTwo guarantees GPU idle and no scheduler DMA. For operations whose ownership and concurrency are protected by the driver, do not request a global drain. NoAdapterSynchronization remains0. Preserve legacy HardwareAccess1 compatibility. RELEASE retains its previous LevelTwo contract because it can reap old scenes while another scene is queued; changing its retirement contract is out of scope.
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-_d3dddi_escapeflags
https://learn.microsoft.com/en-us/windows-hardware/drivers/display/threading-and-synchronization-second-level

AGX/ASAHI CONTRACT:
RENDER_ONLY. Asahi mmu.rs map_in_range and unmap_range use VM execution locks and per-VM ownership; Uat shared/binding/flush locks govern table visibility. Current m1n1 hv_agx_gpuva_v5.c publish_entry rejects live jobs for the affected owner, synchronizes tables and invalidates only that owner's occupied slots; failures roll back or taint. GraphTryLeafBacking rejects own JobInFlight; KMD state lock serializes graph/client/private-pool access. These protections remain. This is not removal of UAT ordering or job exclusion.

TRANSLATION:
UMD ACQUIRE/PREPARE request Flags0. KMD accepts Flags0 or legacy1 for those operations, rejects unrelated flag bits, and continues to require1 for RELEASE. Before reaping or allocating, ACQUIRE/PREPARE must pass the owning graph's JobInFlight/LeaseToken check after the existing bounded wait; a timeout returns busy before any reap. Device/context/process validation and pool/VA/generation checks remain unchanged. No backend, IRQ, memory layout, capabilities, NO_REDIRECTION or firmware modification.
ATOMIC CONTRACT: UMD request flags, KMD accepted flags/RELEASE exception, and placement of own-process quiescence before reap form the entry invariant; changing only caller or callee rejects the request, and allowing software entry to reap after a failed quiescence wait would weaken ownership safety.

WHAT IS STILL UNKNOWN:
Whether removing the unnecessary global drain from this measured ACQUIRE path allows DWM to finish fullscreen/window composition and reach physical output. A stack proves location, not permanence or sole root cause. Verify in the existing boot; do not call stable/fixed solely on a successful update.

Inspected: current umd_gpuva_windows.c private_escape; gpuva_g3_windows.c PrivateEscape/PrivateReap/PrivateTables/PrivateMapExtent; apple_agx_gpuva_g3_graph.c; m1n1 hv_agx_gpuva_v5.c publish_entry; local Asahi mmu.rs; Mu MemoryInitPeiLib reserve ownership (unchanged). Windows owns device/allocations; KMD owns scene allocation and graph synchronization; m1n1 owns UAT hardware publication; GPU execution/completion remains existing backend owner. Accepted normal recovery377/392 remains immutable, but user currently forbids reboot, so no automatic recovery launch.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The actual KMD must admit independently synchronized ACQUIRE/PREPARE while preserving private range ownership, scene limits, completion/release behavior and broker publication checks. Replay real Mesa prepare -> KMD -> m1n1 broker with software entry; retain RELEASE1. Existing legacy-entry lifecycle remains covered. Test16/64 underASan/UBSan, WDK build/sign/hash before any installation.

Live update: user explicitly requests no OS reboot. Preserve exact928 artifacts/evidence first. Prepare hash-pinned929 package and supported PnP update without /reboot or /force. Rearm only the specific experiment StartDevice gate immediately before replacement; inspect result and actual loaded driver identity in the same boot. If Windows requires reboot or cannot unload, record the refusal and do not reboot or force memory patching. Do not invoke legacy rollback scripts that schedule shutdown.
