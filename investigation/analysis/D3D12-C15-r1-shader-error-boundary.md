# C15 typed shader failure boundary after root provider registration

Status: source-first HOLD for publishing the R1/CORE_0010 shader table. This
is an offline architecture decision, not a DDI change or hardware run.

Primary Windows sources: pinned WDK26100 `d3d12umddi.h` SHA256
`d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`
lines 2089–2115 and 3269–3296; Microsoft
[CreateShader_0010](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_create_shader_0010),
[Handling Errors](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/handling-errors),
and [context-local DDI handles](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/using-context-local-ddi-handles).
Current implementation sources:
`agx_d3d12_ddi.cpp` old0003 shader Calc/Create and `ReportRejectedDeviceError`,
`agx_d3d12_r1_shader_span.h`, `agx_shader_input_provider.h/.cpp`,
`agx_d3d12_shader_owned_core.h/.cpp`, and the previously inspected exact
D3D12Core26100.9549 successful ordinal2/3 path in the C15 R1 shader owner
boundary note. EXP1681 root provider run is active; it does not publish
CORE_0010 or execute typed shader callbacks.

WINDOWS CONTRACT: `PFND3D12DDI_CALC_PRIVATE_SHADER_SIZE_0010` returns
`SIZE_T`; `PFND3D12DDI_CREATE_SHADER_0010` returns `VOID` and receives an
unsized `pShaderCode` pointer, root handle, IO-signature pointers/union, flags
and LibraryReference. `pfnSetErrorCb` is the supported way for a VOID UMD
create callback to report permitted errors; Microsoft warns that an error
outside the callback's allowed category is critical and can remove the
device. The current public `CreateShader_0010` page states the signature but
does not name that callback's exact allowed error set. Thus neither
`E_INVALIDARG` nor a generic `E_FAIL` may be sent for a provider/parser
failure merely by analogy with a different DDI. `S_OK` must not be sent through
`pfnSetErrorCb` to reverse an error.

AGX/ASAHI CONTRACT: the linked CPU provider copies canonical Root bytes and
can acquire a typed PROGRAM/signature lease, returning distinct Invalid,
NotLiveRoot, WrongDevice, ProjectOOM, Exhausted, Reentrant and core statuses.
The owned core requires explicit readable spans. The span helper only reads
the first eight trusted bytes and reports a declared length; it does not prove
the body is readable. No GPU, Mesa Asahi, KMD, m1n1 or Mu operation resolves
this CPU memory/error taxonomy.

TRANSLATION: keep typed shader callbacks unregistered. First implement an
internal, caller-trusted CPU adapter that (a) validates a live same-device
Root token, (b) checks PROGRAM and IO count arithmetic before obtaining a
provider lease, (c) uses separate pinned Calculate and Copy operations so a
failure cannot publish partial private storage, and (d) returns internal
status without calling a runtime callback. Test root retirement, wrong-device
handles, output capacity, OOM and lease cleanup under actual source plus the
pinned WDK type signatures. Resolve the exact 0010 runtime error category and
body-readability premise from a primary source/controlled successful runtime
path before connecting failure statuses to `pfnSetErrorCb` or advertising the
table. ProjectOOM may map to E_OUTOFMEMORY only if that callback's documented
category permits it; invalid/unsupported values need their own proven path.

WHAT IS STILL UNKNOWN: the exact allowed error set for 0010 CreateShader,
D3D12Core's trust/extent guarantee for every PROGRAM path, IO-signature
pointer lifetime across Calc/Create, provider callback reentry during device
teardown, and converter internal recoverable OOM. No capability bit or
hardware probe will answer these deterministically. The next falsifiable
checkpoint is an actual-source CPU adapter regression with injected provider
failures and no registered DDI; a separate Windows ABI and production-link
check follows. A J313 run is justified only after a complete truthful
CORE_0010 table and compiler/owner mapping exist.

## EXP1744 internal Standard preflight checkpoint

The actual-source fixture now calls an unregistered
`AgxD3D12R1CopyStandardShaderPreflight` wrapper. It checks the first eight
caller-trusted PROGRAM bytes against an explicit maximum and checks Standard
IO counts, pointers and multiplication against an explicit entry limit before
acquiring a provider lease. On these deterministic failures it returns
`Invalid`, clears layout/publication and does not reach the device failure
sink. A valid actual DXIL PROGRAM plus one Standard input entry still copies
owned bytes through the existing separate Calculate/Copy operations.

The new fixture failed first on the missing function (four undeclared calls),
then the source-bound host ASan/UBSan provider fixture passed 96 checks with
empty stderr. The pinned Windows ARM64 WDK syncheck compiled both DDI
variants and winsys with rc=0; the builder's header SHA-256 matched the
current source. This is a caller-trusted internal CPU preflight only. It
cannot prove body readability, pointer lifetime across separate runtime
Calc/Create calls, or permitted `pfnSetErrorCb` values. `CORE_0010` stays
unregistered and SHADER_MODELS unchanged. The next source-first step must
resolve those typed runtime premises before a hardware admission experiment.
