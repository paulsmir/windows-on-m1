# EXP505 first failing Present observation

WHY THIS HYPOTHESIS: EXP504 removes the source-addressC000000D and physically
latches DCPswap10; the new Event494 is Driver failed PresentC000000D followed by
PresentFromCdd. Production Present's first guard conflates DMA presence, flags,
allocation union presence/counts, private data and destination handle. No actual
inputs are recorded, so no specific guard may yet be changed by inference.

WINDOWS CONTRACT: FULL GRAPHICS WDDM3.0 DXGKDDI_PRESENT is PASSIVE_LEVEL in
pinned WDK26100 and Microsoft docs. It receives driver context/device plus
DXGKARG_PRESENT; DMA copies and MMIO flips have different output requirements.
Pinned header confirms pAllocationList/pAllocationInfo aliases in one union;
their types are different and docs label allocation-info reserved. This does
not prove which actual guard failed. No allocation union is dereferenced by
the new observation. Source/destination rectangles and one valid subrectangle
are scalar input only. No undefined array entries/private command data are read.

AGX/ASAHI CONTRACT: no AGX, DCP, DMA, UAT, power, interrupt or platform protocol
change. Accepted retained-root/runtime/initial modeset and physicalswap10 latch
remain accepted from EXP475/477/478/494–504. Current scanout ownership mapping,
m1n1 native477 and Mu406 are unchanged. No external code copied.

TRANSLATION: before each existing Present failure return call a passive receipt
routine with exact branch/status. First failing call claims one driver-lifetime
slot and writes a zero-initialized160-byte snapshot to device/service keys.
Capture input presence, flags, sizes/counts, DMA location and rectangles only.
Original guard order, handle validation, return values and output buffers remain
unchanged. No success-path instrumentation, worker, allocation or new runtime
decision. Existing context/device lifetime protects device-to-adapter lookup;
the static diagnostic claim resets on module unload. Persistence errors never
change returned status and the single receipt is never overwritten.

WHAT IS STILL UNKNOWN: actual initial CDD Present inputs and exact rejected
guard/operation. Internal Windows ordering is measured once. No memory copy,
AGX submission, TA3D, completion, fence, repeated present or acceleration is
proved by this observation. No interactive user exists; natural CDD is the
current actionable producer without credential/session workarounds.

Sources: EXP504 exact ETL/Event494/host/receipt; production callbacks.c,
receipts.c, render_admission.h/render_objects.h; builder pinned26100
d3dkmddi.h DXGKARG_PRESENT/DXGKDDI_PRESENT/DXGK_ALLOCATIONLIST from
.local/experiments/EXP505-present-receipt/wdk26100-present.txt; official Microsoft
DXGKDDI_PRESENT and DXGKARG_PRESENT pages. Current owning scanout/interrupt
platform references remain as inspected and recorded in EXP504 plan.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? This is receipt-only observation,
not a deterministic behavior fix. No artificial RED. Relevant existing render/
WDDM suites, source review, pinned analysis/Universal/build/sign/version/hash
gates apply. One natural505 bind must persist first failure with branch/status
and scalar args; absent/malformed observation is inconclusive. Evidence and exact
package cleanup precede ordinary377/392 restore. Same long-lived agent immediately
derives the deterministic translation or next smallest discriminator.
