# C15 internal shader input transaction before typed DDI admission

Classification RENDER_ONLY CPU-only. Primary sources inspected: pinned
WDK26100 `d3d12umddi.h` `_0010` shader signatures; current
`agx_shader_input_provider.h/.cpp` AcquireInput/BeginOperation/
CalculatePinned/CopyPinned/EndOperation/ReleaseInput state machine;
`agx_d3d12_shader_owned_core.cpp` explicit PROGRAM/IO/root span copy; and
Microsoft [CreateShader_0010](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_create_shader_0010)
and [Handling Errors](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/handling-errors).
The Mesa/Asahi owner and m1n1/Mu/KMD do not own this CPU lease lifetime.

WINDOWS CONTRACT: typed 0010 Calc returns SIZE_T, Create returns VOID. The
runtime callback error category and full body readability remain open.
This helper is not a DDI callback, receives explicit caller-certified
readable/stable spans and never invokes `pfnSetErrorCb`.

AGX/ASAHI CONTRACT: provider Root registration copies canonical bytes and
pins them for an input lease; the owned core copies PROGRAM/signatures/root
into pointer-free storage under a pinned operation. All source pointers
stay stable until ReleaseInput. No GPU, queue, owner shader-state or DXIL
converter operation is added here.

TRANSLATION: one internal function obtains a provider lease, begins a one-use
operation, calculates exact owned storage, copies only into caller-provided
capacity, ends the operation and releases the lease on every return path.
It zeros the caller's output pointer/layout on refusal and exposes the
provider/core status without converting it to HRESULT. An unexpected End or
Release failure suppresses publication; such cleanup failure remains an
internal error rather than a silently successful shader. This is not
permission for an exception to cross a future C/VOID DDI boundary.

WHAT IS STILL UNKNOWN: whether every 0010 PROGRAM and signature body is
readable/stable during Calc/Create, the exact allowed runtime error category,
provider callback reentry during device teardown, full converter internal
OOM, and truthful CORE_0010 table/caps. No hardware run can resolve the
internal lease cleanup invariant while this function is unregistered.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A failed or undersized copy
must not leave a live provider lease/operation or a published shader pointer;
a successful copy must retain its canonical root and PROGRAM bytes after
source mutation. Test with the actual provider and owned core on pinned DXIL
and legacy fixtures, then WDK ARM64 syntax and source-bound production link
later. This is an offline checkpoint, not SM6 admission.

Offline result: the first actual-provider candidate (EXP1684) compiled but
failed because it tried CalculatePinned and CopyPinned with one one-use
operation token. The actual provider moves Ready→Running→Finished during
CalculatePinned, so CopyPinned correctly refused that token. EXP1685 corrected
the function to use the provider's Calculate and CopyOwned wrappers, which
each create and end a distinct pinned operation on the same lease. The exact
owned-core/provider/zero-cache host build on pinned DXIL and legacy PROGRAM
fixtures returned four rc0 with Appleclang21 ASan/UBSan and `checks=90`,
including success, undersized storage cleanup and wrong-root refusal. A
final fresh render after marker update repeated checks90/rc0.

EXP1686 staged DDI SHA256
`cbde10e9be5ef9e26562bde06f7f855f5b12b018c2a261d370a2f96b78ddd7a3`
and uncalled transaction header SHA256
`0ef1fe61b4d09e45be1250ed4f57d7ac45ae0f69c92f86de060a97b7b75e8272`;
post-stage hashes matched. Pinned WDK26100 native-owner/NONE/winsys syntax
returned three rc0, raw builder log
`.local/c15-r1-shader-transaction-syntax1686/builder.log` SHA256
`ddac7d6b56ae5ad6b0982f2191bba1037db051b79879fc66b6a51330c42730e1`.
Focused ABI/native-owner/change-ledger tests32 PASS; provider unittest skips
without its separate approval, and the direct actual-module fixture is the
recorded host verification. No production link, PE execution, DDI table or
J313 shader transaction result is claimed.
