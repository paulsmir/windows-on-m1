# EXP672 fragment-output to tile-store comparison

Date: 2026-09-09.  Scope: the unchanged 16x16 V13_5 triangle used by
EXP665--EXP672.  This is offline evidence; it is not a hardware verdict.

## Primary sources inspected

- Asahilina Mesa `7a4f24061fa56ef7eff12132dd7b1461d5a890d8`:
  `src/gallium/drivers/asahi/agx_state.c` (`agx_set_framebuffer_state`,
  `agx_build_store_pipeline`), `agx_pipe.c` (`agx_cmdbuf`), `agx_blit.c`
  (clear/store shaders), `src/asahi/lib/cmdbuf.xml` (Render Target and USC
  layouts), and `src/asahi/compiler/agx_compile.c` (`agx_emit_fragment_out`).
- EXP659 immutable archive:
  `.local/experiments/EXP659-native-triangle/evidence/frame` and
  `producer.log`.
- Current production path:
  `submission_windows.c`, `render_backend_image.c`,
  `apple_agx_exp208_gdi.c`, `render_dynamic_overlay.c`,
  `apple_agx_render_shared_memory.c`, and `backend_platform_windows.c`.

## Exact production order

1. `AdmissionBackendImagePrepare` materializes, rebases and applies the EXP208
   relocation table.
2. `AdmissionDdiSubmitCommand` calls
   `AdmissionBackendImageBindDynamicSubmission` before queuing the request.
3. The dynamic bind calls `AppleAgxExp208AdoptNativePipelineLayout`, binds the
   exact destination, patches the clear/store USC buffer addresses and applies
   the generated relocations again.
4. The worker opens the sealed dynamic DMA and applies the typed overlay.
5. `AdmissionBackendImageStageJob` applies the generated relocations and only
   the stamp/event dynamic patch set.
6. `AppleAgxRenderSharedMemoryBuildActiveJob` copies the already transformed
   object18 into the queue-owned WorkCommand, applies only generated
   per-submission relocation edges, and leaves objects36/73/74 as the same
   source-owned immutable/bound data.
7. `AdmissionDynamicOverlayRouteEncoder` changes only object19+0xd0.
8. The final WorkCommand/store graph is flushed/barriered and published.

The generated relocation table has no source73 entry and no source18 entry at
0x90, 0x3cc, 0x618, 0x648, 0x714 or 0x734.  Therefore neither the second
general relocation pass nor active-job relocation restores the pre-adoption
store slots.  Executable tests now enforce this through
`AdmissionBackendImageBindDynamicSubmission` and
`AppleAgxRenderSharedMemoryBuildActiveJob`.

## Semantic comparison

| Field / object | Native EXP659 value | Final production value | Owner / source | Expected difference | Unexplained difference |
| --- | --- | --- | --- | --- | --- |
| Fragment shader | 112-byte constant-red FS; `st_tile u8norm xyzw`; native VA `0x110006c000` | Same 112 bytes and fixed VA; EXP669 FNV `0x8964d9ebaac8b869` | Typed dynamic overlay; fixed context-63 alias | None | None in captured bytes/address |
| Fragment USC | `4d bd 10 20 0d 09 00 c0 06 00 8d 20 00 01 58 f3 02 00 88 00` | Byte-exact after typed shader relocation | Dynamic pipeline page `0x1100021000` | None | None in offline composition |
| Fragment shared state | 32x32 layout, shared enabled, pixel stride one 8-byte unit, 32 units of 256 bytes | Same encoded word `0x2010bd4d` | Pinned `USC Shared` contract | None | None |
| WorkCommand pipeline slots | clear `0x22004`; store/partial store `0x24004`; reload `0x23004` | Same six final fields after bind, stage and active-job build | object18 plus adopted layout | None | EXP669 did not capture these runtime fields |
| Clear pipeline | Native page `+0x2000`; normalized FNV `0xb4f04eb2fd87523d` | object73 `+0x2000`, FNV `0xb4f04eb2fd87523d` | EXP208 clear owner; destination-state address rebased | Only uniform address | Not in EXP669 runtime receipt |
| Reload pipeline | Native page `+0x3000`, FNV `0x9d88404f8bc9022b` | object73 `+0x3000`, FNV `0xe0ccbe718205f3cb`; code/control tail matches, legacy texture/sampler addresses remain | EXP208 reload owner | This clear workload has flags=0 and uses clear load, not colour reload | Runtime value not yet captured; not a current causal mismatch |
| Store pipeline | Native page `+0x4000`; FNV `0xfc0be24f53900f7e` | After normalizing only state-object addresses, full-page FNV `0x30c354d48249bb26`, exactly equal to normalized native page | EXP208 store owner plus destination bind | Texture/uniform addresses point at rebased object36 | Not in EXP669 runtime receipt |
| Store shader | `imgwblk u8norm`; code at `0x1100010400`; first-256 FNV `0xc3387ebea1b9f34d` | Same code hash; object74 is additionally fixed-mapped read-only at `0x1100010000` | Firmware-visible fixed input object74 | Backend-arena alias is expected; executed VA stays native | Not in EXP669 runtime receipt |
| Render target descriptor | q0 `0x000003c00fc60a22`, q1 `0x100000015001d000`, q2 zero; tiled, format40, BGRA swizzle 2/1/0/3, 16x16 | q0 identical, q1 `0x10000001500fa000` for EXP672 destination, q2 zero | object36+0x3000 and bind owner | Only exact destination address | Not in EXP669 runtime receipt |
| Store companion | `{0, 0xffffffff, 0, 0, ...}` at native state+0x7000 | Byte-exact at object36+0x4000 | EXP208 state object | Address only | Not in EXP669 runtime receipt |
| Output attachment | Native attachment pointer `0x15001d0000`, size `0x4000` | EXP672 target GPU VA `0x1500fa0000`, size `0x4000`; generated object15 relocation owns it | Windows allocation / generated relocation | Exact Windows-owned destination | EXP669 did not capture the final active microsequence pointer |
| Lifetime | All native objects live through completion | Backend image, fixed inputs and Windows allocation are held through fence271/output verification | Existing backend/output ownership | Different owners, same required lifetime | No lifetime loss is visible in EXP672 |

The correct 184 background pixels independently prove that the clear,
tilebuffer and store path is operational for this tile.  The exact 72-pixel
zero triangle mask proves raster coverage but does not by itself prove the
final active store fields above were the values consumed by that job.

## Result and next discriminator

No source-backed semantic mismatch remains in the final deterministic values
available offline.  The one missing runtime scalar set is the final active
WorkCommand/store graph for the exact completed fence.  The next candidate is
therefore observation-only: capture those bounded fields in memory after
`BuildActiveJob`/encoder routing and export them from the existing PASSIVE
output worker as `Wom1DynamicStoreReceipt`.  It performs no registry I/O,
waiting or rendering mutation before submission.
