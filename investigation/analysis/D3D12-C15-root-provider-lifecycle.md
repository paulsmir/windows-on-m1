# C15 root registration in the current D3D12 UMD

Status: design checkpoint after EXP1663 J313 ordinary root PASS and EXP1664
CPU-only range arithmetic. Classification RENDER_ONLY UMD metadata. This
note proposes no CORE_0010 table or SM6 caps change.

Primary sources inspected: pinned WDK26100 `d3d12umddi.h` root types and
`D3D12DDI_DEVICE_FUNCS_CORE_0003` Calc/Create/Destroy callbacks; current
`agx_d3d12_ddi.cpp` private Device/Root structures, callbacks and C5 root
binding; `agx_d3d12_r1_root_canonical.h` explicit-extent encoder;
`agx_shader_input_provider.h/.cpp` Device/Root registry/lease lifetimes;
`AppleAgxRenderAdmissionUmd.vcxproj` production provider source inclusion;
EXP1615 production provider link receipt, EXP1663 signed package and J313
root/ordinary rendering receipts. The existing Asahi owner takes resolved
root binding commands only at replay. No m1n1/Mu/KMD/ACPI protocol changes.

WINDOWS CONTRACT: the pinned old0003 runtime allocates private Device and
Root storage from Calc sizes and invokes Create/Destroy with those handles.
CreateRootSignature can fail with HRESULT; DestroyRootSignature and
DestroyDevice are VOID. User-mode Device and Root private storage remains
runtime-owned, and callbacks can be free-threaded where WDK allows. No
callback can retain an unpinned runtime-private pointer beyond its lifetime.

AGX/ASAHI CONTRACT: the native owner and KMD see exactly the existing root
commands. The CPU provider copies canonical root bytes at registration and
pins them for shader input leases; a retired root refuses new leases but an
already admitted lease keeps its copy. No GPU action occurs while the provider
registry mutex is held. No bytecode translator or AGX queue is called by
register/retire operations.

TRANSLATION: one provider instance and Device token live in each current
`AgxD3d12Device`. CreateDevice initializes/registers them before publishing
the existing live-device identity; failure leaves no published identity and
returns an exact HRESULT. The checked old0003 Root Create builds the current
private layout, measures its canonical wire length from the just-validated
private extent, encodes to temporary CPU memory, registers the provider-owned
copy under the creating Device token, and stores only the resulting Root token
in runtime-private Root storage. Free the temporary bytes on every path.
DestroyRootSignature retires the token before runtime frees private storage;
DestroyDevice retires the Device, closes provider admission and destroys the
provider after all callback work has quiesced. The existing live Device/PSO
identity registry must remain address-safe and cannot be replaced by a
borrowed provider resolver without a separate concurrency proof. CORE_0003
callback table and SM5.1 caps remain unchanged; typed shader callbacks still
fail closed.

WHAT IS STILL UNKNOWN: actual callback destruction order and memory pressure
on J313 once the provider is linked and registered in the current old0003
path. Source-bound CPU lifecycle fixture must verify create/root register,
canonical copied bytes and retirement, invalid root rejection without
registration, failed allocation cleanup and multiple Device isolation. Pinned
WDK syntax then full production source/link/tlog/import/stack/sign gates
precede one isolated signed J313 run. The hardware discriminator is the
accepted 63 unique non-E D3D12/root, pool/FL11/corpus/DWM/GL/CS matrix,
plus provider-specific success/retire diagnostic receipts. FactoryV2 exact
visible recovery to Code28/PackageAbsent/noService/8CPU is mandatory.

ATOMIC CONTRACT: Device registration and Root registration/retirement are
one provider-owned lifetime invariant for root compatibility bytes. A Root
token without a live Device token is invalid, and a Device provider without
Root retirement cannot offer a safe shader input lease. No independent
capability or AGX owner operation is bundled. EXP1664 range arithmetic will
enter the next source package only as separately verified deterministic
safety; its absence/presence must be explicit in the source diff and BEFORE.
