# C25 source proposal — multi-Present / ResizeBuffers lifetime

SOURCE ONLY. No compiler, native execution, guest, package, signer, or tracked-file mutation. Probe source is actual accepted1509 (C21E remains SKIP); all28 generated shader headers remain unchanged. The sole additional SDK include is pinned26100 dxgi1_4.h for the actual current-back-buffer query; no guessed alternating index.

WHY THIS HYPOTHESIS:
1. Existing C7A source lines2276–2348 gets buffer0 once, clears once, presents once, then releases the swapchain and device. It never retires/replaces backbuffers while the owner survives.
2. Actual normal1504 owner collect_presentations (lines69–87) retains released presentation records while AdmissionUmdScreenAllocationRegistered remains true. DestroyPresentationTexture (739–752) releases native render backing, releases runtime resource, appends PendingPresentations and collects. CreatePresentationTexture (667–678) retries collection. Resize/recreate is therefore a distinct real lifecycle boundary from process/device teardown.
3. The actual DDI ReleaseHeap (870–876) routes presentation destruction to that owner method; DestroyHeapAndResource (1195–1202) clears runtime resource storage. Public ResizeBuffers drives that boundary without private pointers or invalid references.

WINDOWS CONTRACT:
FULL GRAPHICS, public DXGI/D3D12. Official IDXGISwapChain::ResizeBuffers remarks require releasing direct and indirect backbuffer references, including views and command lists, then reacquiring buffers. Explicit two-buffer flip-discard, RGBA8, samplecount1 remains the old C7A shape. Source: https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers (read2026-10-19, remarks79–84). Local SDK26100 dxgi1_4.h lines482/554 supplies real ResizeBuffers/GetCurrentBackBufferIndex; d3d12.h lines5150–5156 defines Windows C GetDesc with an output pointer (candidate uses that exact form).

AGX/ASAHI CONTRACT:
Existing owner imports presentation allocation as a linear RGBA/BGRA texture (owner667–735); PreparePresent (953–968) flushes the actual context and publishes the direct map before handing runtime its allocation. Owner retirement is deferred until screen unregister. No new layout, allocation, hardware register, or backend mapping is proposed.

TRANSLATION:
Use existing run_isolated fresh device/queue/fence lifetime, after original C7A. One C25 swapchain: three frames at64x64, release every frame's list/allocator/RTVheap/readback/backbuffer after proven rendering and post-Present queue fences; ResizeBuffers once to96x48 with zero old application references; reacquire current buffer and recreate view for three more frames. Obtain actual IDXGISwapChain3 current index; require both indices visited per epoch. Each frame clear cycles exact R/G/B, copies BEFORE Present into GetCopyableFootprints readback, verifies every active RGBA byte using returned pitch, restores PRESENT, presents and signals/waits. Six unique monotonic render/post-Present fence pairs. Always fence a Present attempt even if Present returns failure; never release resources after unproven completion (process exits, evidence retained). No post-Present content oracle because FLIP_DISCARD contents are undefined.

WHAT IS STILL UNKNOWN:
Actual DXGI/runtime/backbuffer retirement and new-allocation ordering against this owner/screen while the device stays live. C7A source and its single-Present result do not establish that behavior. This is the hardware discriminator, not a predicted driver bug. It does not prove every resize/fullscreen/format/count transition, kernel allocation leak freedom, visible pixel scanout, or general DWM concurrency.

Checkpoint / recovery:
One new AGX_D3D12_C25 PASS/FAIL receipt; six per-frame index/size/pixel/bad/HRESULT rows, one Resize HRESULT row, final frames6/resizes1. PASS requires all active pixels exact, both current indices observed per epoch, all Present/fence operations successful, resize S_OK and final totals. Any failure remains a measured first boundary, no repair guessed. Existing59 non-E controls remain unchanged; C21E SKIP stays unchanged. ATOMIC CONTRACT: none — one independent probe-only lifecycle step, no DDI/capability/firmware change.

Root may later preregister a fresh run with exact unchanged signed1506 package30.0.1506.0, explicit experiment/package version override, prior1510 recovery proof and fresh hash-verified reinstall. Preserve ordinary FactoryV2/normal GPU-visible recovery; collect evidence before removing exact run package. No launch authorization or experiment ID is created by this proposal.

Offline gates still required:
Root source review; actual SDK ARM64 probe compile/import/AA64/hash gate with unchanged28shader headers; source-diff audit baseline+C25 only. No compilation was performed here. Readback copy from presentation is intentionally part of the measured public lifecycle contract; source path accepts native2D resources, but no claim that C7A already verified that combination.

Precompile source review correction: unique fresh-fence values require done==target, rejecting UINT64_MAX device-removed sentinel. Footprint must have nonzero pitch>=row_bytes, offset<=total, row_bytes<=total-offset and (rows-1)<=(total-offset-row_bytes)/pitch before CPU addressing. Division is guarded by short-circuit nonzero-pitch check; no guessed alignment constant. Original proposal preserved in revision-before-fence-footprint.
