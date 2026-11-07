# C26 source proposal: immutable-list replay with a replaced volatile CBV

SOURCE ONLY, uncompiled. No production/DDI/capability/build-hook changes. This is not a claim of a driver defect or hardware approval. The current C25 run is not analyzed here; future adoption would require its accepted verdict/recovery separately.

WHY THIS HYPOTHESIS:
1. Actual1509 C5C records/closes one list, stages descriptors once, executes once, and checks 16,384 words. C6A resets/re-records for its second submission. Neither validates re-executing an unchanged closed list against a newly replaced shader-visible descriptor.
2. Normal1504 DDI1845–1855 deliberately retains CBV/UAV descriptor locations, unlike RTV descriptors which are copied at recording. OwnerBindings3360–3380 dereferences those locations during ReplayDispatch3450. A cached descriptor or cached owner binding on second execution would silently keep the first constant-buffer resource.
3. OwnerDispatch1529–1556 rebinds/unbinds constant buffers around the actual launch. Only real repeated execution with two distinct live constant-buffer allocations checks that path all the way through the native context/cache. Existing first-use compute success does not establish the second binding behavior.

WINDOWS CONTRACT:
FULL GRAPHICS public D3D12, root signature1.0, direct command list, one queue. Microsoft root-signature documentation DESCRIPTORS_VOLATILE section explicitly permits descriptor changes while no referencing execution is unfinished and identifies this as root1.0 behavior. https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-version-1-1 (section85–88, read2026-10-19). ExecuteCommandLists permits resubmission only with valid allocator/completion lifetimes; this proposal waits for each unique fence before the next submission and never resets/releases the list or allocator between executions. https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists . PinnedSDK26100 d3d12.h defines the actual CBV/table/Execute interfaces. No1.1 promise/flag is introduced.

AGX/ASAHI CONTRACT:
Actual owner bind_stage sets pipe constant buffers (owner1384), then OwnerDispatch binds existing compute state and launches grid (1529–1556); unbind_stage clears constant buffers afterward (1481). No new shader/backend/opcode/format/BO or lifetime guarantee is invented. Inputs/resources remain live across both fully fenced launches.

TRANSLATION:
Reuse the actual validated C5C shader/header and root-table shape {raw UAVu0, CBVb0}. Allocate two upload constant buffers BEFORE recording. Both hold additive3; coefficients differ7 versus11. Record and close exactly one list: dispatch, UAV->COPY_SOURCE, copy64KiB readback, COPY_SOURCE->UAV. First execution uses CBVbufferA and requires all16,384 words=7*i+3. After exact fence completion, replace only the existing shader-visible CBV slot with bufferB (both resources/heaps remain alive). Re-execute the SAME closed list, no Reset/rebind/re-record, and require all16,384 words=11*i+3. This catches stale resource/descriptor binding (16,383 wrong words if coefficient remains7). Readback Map/Unmap follows each fence. Final AGX_D3D12_C26 PASS requires both complete exact oracles; per-pass receipt includes identical list pointer, coefficient, word count, bad count, sample words and completed fence. No GOLDfile, shader compile, or new descriptor namespace needed.

WHAT IS STILL UNKNOWN:
Actual runtime-to-UMD repeated-list replay and the native context's descriptor/resource rebinding between completed executions. Source shows the intended deferred descriptor lookup; it cannot substitute for the full runtime/native execution receipt. No suspected register/ABI fix is proposed. This finite checkpoint does not prove arbitrary mutable heaps, descriptor update during flight, resources freed early, multi-queue reuse, OOM, or production C15 integration.

Checkpoint / recovery:
Root may approve this source after review, then actualSDK ARM64 compile/hash/import gate, then a fresh probe-only run on the unchanged signed1506 package with explicit version override. Expected new line AGX_D3D12_C26; old59 non-E and C25 controls remain unchanged, C21E remains exact known SKIP. No driver/capability/firmware variable. ATOMIC CONTRACT: none — one lifecycle checkpoint (second execution resolves the replaced volatile descriptor). Keep ordinary FactoryV2 GPU-visible recovery, collect evidence, remove the exact experiment package, verify PackageAbsent. No ID/hardware marker is reserved by this proposal.

Bounded alternative rejected:
RTV record-time descriptor replacement can first be tested wholly offline because ClearRenderTargetView directly copies the descriptor by value (DDI2130–2143); using hardware as its primary discovery tool is unnecessary. C26 instead tests deferred shader-visible lookup through completed native executions.

Source limitations:
Candidate is source-only and not compiled; actualSDK build gate remains mandatory. No test, executable, native compiler, guest/remote, Git mutation or tracked edit was performed. All28 shader headers are unchanged from1518. Existing isolated-run adapter/device/queue/fence construction is reused; no independent-device assumption is added.
