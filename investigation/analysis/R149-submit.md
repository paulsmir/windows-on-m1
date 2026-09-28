# R149 USC address contract — offline current-source pass

## Captured facts and source inference

EXP862 first G4 receipt: Render ordinal14, read1 at0x11002b0140,
GraphPresent0, root0x9d6aa0000, mapping generation474; eight failures.
Raw native packet, field-name tag, BO token/base/size were not captured.
Ordinal is dynamic. Under the pinned no-helper/no-sampler packet shape,
nine private ranges(0–8), envelope9, attachment10, VDM11, scissor12,
dbias13 place Bg.Usc at14. That field identity remains producer inference.
The expanded USC address and the source-wide USC coordinate disagreement are
independent of which nonzero USC field occupies ordinal14.

Pinned Mesa 9aa1215f878b504f66159dd2ead4c7973142126e:
`agx_pipe.c:1374` assigns bg.usc from pipeline_clear.usc; `agx_state.c:3101`
allocates the pipeline in batch->pipeline_pool and :3236 truncates t.gpu
into the 32-bit USC field. `agx_batch.c:102` creates that pool LOW_VA;
`pool.c` returns BO VA+cursor. Thus the inferred Bg is an Encoder-class,
non-EXEC pipeline-pool slab, distinct from shader machine-code BOs. Its
actual hardware base/token cannot be reconstructed by rounding this address.

Current `agx_win32_asahi_bo.c::AgxWin32AsahiAttach` sets shader_base=0 in
GPUVA mode; `agx_win32_gpuva.c::AgxWin32GpuvaBind` reserves LOW_VA below4GiB.
Both parser `apple_agx_g4_submit.c::usc_va` and builder
`apple_agx_g4_builder.c` use execution base0x1100000000. Thus a low mapped
pipeline address0x2b0140 is validated/executed at0x11002b0140. Pool residency
cannot repair this mismatch. The failure lies in UMD VA placement/encoding,
not an absent KMD-private publication or evidence for bypassing validation.

## Inspected contract / implementation plan

Primary sources: pinned Mesa agx_device.c/.h (4GiB-aligned shader_base,
4GiB USC heap, agx_usc_addr subtraction), pool.c, agx_state.c, agx_pipe.c;
Asahi Linux mmu.rs user range[16KiB,2^39), queue/render.rs USC_EXEC_BASE_ISP/TA
from VM queue base; m1n1 hv_agx_gpuva_v5.c::update_leaf complete16KiB native
leaf publication; Mu J313AppleAgxAbiAdmission.asl.inc APPL0002 resource
contract; current gpuva_g3_windows.c CreateProcess private VA reservation;
UMD umd_gpuva_windows.c reserve/map callbacks; G4 parser/builder above.
Microsoft D3DDDI_RESERVEGPUVIRTUALADDRESS documents minimum and exclusive
maximum (Base+Size<=Maximum),64KiB alignment, process-owned reservation:
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-d3dddi_reservegpuvirtualaddress
GPUVA/VidMm ownership:
https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-virtual-memory-in-wddm-2-0

UMD owns BO placement, native relative encoding, residency selection and upload;
VidMm owns unique process reservations/mappings; KMD owns mapping publication
and parser enforcement; m1n1 owns validated UAT publication; Mu owns resource
exposure. IRQ/power/DMA firmware contracts do not change. R137 private memory
uses a separate OS-reserved32MiB aligned interval, returned by VidMm, not a
fixed USC range. A runtime may place it inside the4GiB USC interval, reducing
available space; successful UMD reservations still cannot overlap that owner.
No particular EXP862 private base is inferred from this receipt.

Plan (authorized parent continuation; no package/hardware/commit here):
1. RED replay with real Attach, Bind, pinned native agx_usc_addr and production
   parser; model only owner callbacks and mapped extents. Show same ordinal14
   mismatch for a synthetic captured-shaped case and independent VA/offset.
2. Define the existing fixed G4 USC execution window in one shared header.
   LOW_VA reservations use[base+64KiB,base+4GiB), preserving nonzero USC offset.
   Reject below-minimum/out-of-range/wrapping callback results before Map.
   Native shader_base uses that same fixed base; reject conflicting attach base.
   Keep general BO range, flags, exact canonical residency and guard behavior.
3. GREEN replay boundary/failure cases and existing G4 pool/builder tests.
   Parent owns full suite, ARM64 /W4 /WX /analyze, review and commits/CHANGES.

Smallest separately authorized hardware checkpoint: same R143 launch contract,
USC UMD placement correction as single behavior variable; advance beyond the
same USC parser rejection. This alone claims no firmware/fence/TA/3D success.
Recovery stays ordinary GPU-visible Code28; no hardware was attempted here.

## Implemented and verified offline

TDD replay first failed at the expected production parser assertion:
`mapped=0x2b0000 shader_base=0 packed=0x2b0144 result=3 ordinal=14
va=0x11002b0140`. The callback model supplies a deliberately synthetic BO
base/offset with the captured shape; this is not a recovered native packet.
See `investigation/evidence/R149-submit/usc-red.txt`.

Implemented shared `APPLE_AGX_G4_USC_EXECUTION_BASE`/`USC_WINDOW_BYTES`
constants, UMD LOW_VA window placement and returned-range validation, and
native shader_base/attach-base agreement. Parser and G4 builder use the same
existing execution-base value. General BO placement, canonical identity,
residency, upload, KMD guards and firmware execution values are unchanged.
The zero-offset64KiB guard preserves the prior allocator's nonzero USC rule.

GREEN: same packed0x2b0144, actual map0x11002b0000, shader_base0x1100000000,
parser accepted. Another pool case uses map0x1100830000/offset0x8c0. An EXEC
helper uses native agx_usc_addr at another address; nonEXEC pipeline and EXEC
helper protections remain distinct. Tests reject zero/below-window/unaligned/
end/overrun/wrapping returned VAs before Map; accept the final complete page;
reject oversize and wrong attach base; preserve uncertain-Free terminal state,
general BO bounds and non-GPUVA construction coordinates.

`tests/test_g4_usc_window_replay.py` extracts the actual Attach and pinned
reference native address helper into temporary build output and compiles the
real Bind/parser under ASan/UBSan with -Wall -Wextra -Werror. The fixture shells
model WDDM callbacks and typed native owner structures, not hardware paging.
No external implementation was copied into repository source. The parser walk
starts after modeled successful private/envelope/attachment ordinals; previous
full-packet/real-broker replays independently retain coverage of those owners.

Verification evidence under `investigation/evidence/R149-submit/`:
- `usc-green.txt`: GPUVA and non-GPUVA replay pass.
- `targeted-green.txt`:7 tests pass (USC, pool, parser, builder, real broker,
  outer SubmitCommandVirtual).
- `g4-affected.txt`:38 G4 tests pass after final test changes.
- `gpuva-affected.txt`:34 GPUVA tests,32 pass and2 missing-llvm-link errors
  (`test_portable_cpu_view_validator`, `test_gpuva_system_context_object_contract`).
- `git diff --check` passes.

These are software-only results. Parent owns independent review, complete-suite
baseline comparison, ARM64 compiler verification, implementation commit and
CHANGES.csv. No hardware package/launch was produced by this work. Later QUERY
VA0x20000/generation690 and SSH stall remain separate investigations; the USC
fix neither diagnoses nor relaxes those boundaries.
