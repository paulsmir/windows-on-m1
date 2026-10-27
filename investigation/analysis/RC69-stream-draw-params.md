# RC69: the GPU indirect stream ran every record with record 0's StartInstance

Offline branch `codex/rc69` (base `6b42ddb2`, integration with rc63-rc66).
No hardware, guest, SSH or experiment directory was written. Evidence (read
only): `.local/experiments/EXP1987-ring-tdr/evidence/EXP1987/tool-rc63-std.txt`
and `tool-rc63-umd.log`; `.local/experiments/EXP1978-probes-ibv/evidence/EXP1978/`
`tool-rc44-std.txt` and `tool-rc44-umd.log`.

## 1. Which field the hardware lost

`tool-rc63-std.txt:23`: `RC63G readback bad=4095 first=1 got=1,1,64,255`;
`:24-26`: statistics PASS (IA 360000/120000, VS 360000, clip 120000, PS 4096).
`tool-rc63-umd.log:523`: `measure-d3d12-counted-path ... 00000003 0000ea60`, so
path 3 (GPU stream), MaxCount 60000.

Probe design (`windows/icd/agx_d3d12_probe.c:7772-7795`):

- record r: IndexCount 6, InstanceCount 1, StartIndex 0,
  `BaseVertexLocation = 4 * (r % 4096)` (`:7776`), `StartInstanceLocation = r` (`:7777`);
- quad q (selected by BaseVertex) covers only the centre of pixel q;
- instance r's UNORM8 coordinate (selected by StartInstance) samples texel r
  for r < 4096; texel i = `((i%64)*4+1, (i/64)*4+1, 0x40, 255)` (`:7780`).

`got=1,1,64,255` at pixel 1 is texel 0. So:

- **BaseVertexLocation was applied**: pixel 1 was covered (a dropped base
  vertex leaves every pixel but 0 at the red clear, 255,0,0,255);
- **StartInstanceLocation was not**: pixel 1 sampled with instance 0, which is
  the StartInstance of **record 0** (and also the value of zeroed memory). The
  same holds for all 4095 pixels: every first record drew with base instance 0.

## 2. Where each field travels (source)

| field | consumer | path in the stream |
|---|---|---|
| IndexCount/InstanceCount/StartIndex/BaseVertex | VDM, from the record (`INDEX_LIST` indirect buffer) | kernel `agx_draw_indexed_indirect(arg, ...)` (`agx_win32_indirect_stream.cl`, old `:70-74`) |
| StartInstanceLocation | VS prolog vertex fetch: `el = instance_id / divisor + load_base_instance` (Mesa `src/asahi/lib/agx_nir_lower_vbo.c:170-176`) | uniform `AGX_ABI_VUNI_BASE_INSTANCE` (`agx_nir_prolog_epilog.c:73-74`) = second word of the four-halfword PARAMS load (`agx_nir_lower_sysvals.c:221-224,405-411`) |
| BaseVertex as SV_VertexID offset | `load_base_vertex`/`load_first_vertex` from the same PARAMS load (`native-asahi-graph-capture.py:24-31`) | unused by the RC63G VS |

Native Mesa points PARAMS at the record's last two words, offset 12 indexed / 8
non-indexed (`src/gallium/drivers/asahi/agx_state.c:4419-4452`). D3D12
`D3D12_DRAW_INDEXED_ARGUMENTS` puts BaseVertexLocation at 12 and
StartInstanceLocation at 16 (`winsdk26100/um/d3d12.idl:3138-3145`), so the
offsets agree; firstInstance == StartInstanceLocation and the index-buffer
view offset travels in the IB address, not in BaseVertex
(`mesa/winsys/agx_d3d12_windows.cpp:4043-4063`).

For the stream the wrapper builds the CPU template with `draw_count = 1` at the
original offset (`native-asahi-indirect-stream.py:186-193`), so the template
USC's PARAMS load points at **record 0** + 12. Per record the kernel copied that
template, repointed PARAMS at `arg + 12` and selected the copy (old
`agx_win32_indirect_stream.cl:59-68`):

```c
agx_push(out, VDM_STATE, cfg)
   cfg.vertex_shader_word_1_present = true;
agx_push(out, VDM_STATE_VERTEX_SHADER_WORD_1, cfg)
   cfg.pipeline = p->pipeline_base + i * p->usc_stride;
```

The host tests prove the generated words match Mesa's encoders. The hardware
result means the per-record PARAMS value never reached the VS: either the
record's USC was not bound (the template's, record 0, stayed in effect), or
its PARAMS load read zero. Two features of the stream are exercised by no
hardware-validated path:

1. **A VDM state update with only vertex-shader word 1.** Every Mesa encoder
   emits word 0, word 1, vertex outputs and vertex unknown together
   (`agx_state.c:3470-3500`, `hk_cmd_draw.c:3107-3130`), and gallium re-emits
   that block per draw whenever the VS uses base parameters
   (`agx_state.c:5048-5054`). `decode.c:671` shows word-1-only blocks are
   decodable, not that the G13 VDM rebinds the vertex shader and reloads its
   uniforms from them.
2. **A PARAMS load from GPU-copied argument memory.** RC63G copies the records
   into a DEFAULT buffer on the GPU. The only hardware PASS with nonzero
   StartInstance through PARAMS is RC44I (`tool-rc44-std.txt:16-18`, path 2
   reason 7 at `tool-rc44-umd.log:899,912`), whose arguments are CPU-written.
   The CDM reads of the same GPU-copied records are proven correct by
   RC63G_STATS.

Offline source cannot tell which of the two the G13 hardware rejects; both
produce exactly the observed readback, and neither is used by the
hardware-validated ordinary path.

## 3. Fix (behaviour change)

- `a820f66e`: the projection captures the template's complete native VS
  block right after `agx_encode_state` pads it
  (`native-asahi-indirect-stream.py:121-135`); each record repeats it with only
  word 1 replaced (`agx_win32_indirect_stream.cl:73-80`), the native per-draw
  form. The emitter refuses a missing or non-native block
  (`agx_win32_indirect_stream_emit.h:124-139,166`). Record: 24 bytes of VS
  state + index list (44 bytes indexed, was 28).
- `912c4de0`: each executed record's two PARAMS words are copied by the
  dispatch next to its USC words and the load points there
  (`agx_win32_indirect_stream.cl:56-71`, layout helpers
  `agx_win32_indirect_stream.h:34-54`). The USC stride grows by at most one
  64-byte step.

## 4. Ordinary counted path (path 2)

`agx_win32_counted_indirect.h:8-66` maps the count and issues one
`draw_vbo(draw_count = min(count, MaxCount))`; native
`util_draw_multi_unroll_indirect` (projected `agx_state.c:4888-4903`) then
draws each record with `draw_count = 1` at `offset + i * stride`. Each draw
runs `agx_upload_draw_params` (PARAMS = record i + 12) and re-emits the full VS
block with a freshly built USC. Semantics are correct, and RC44I validated
them on hardware with StartInstance 3/5. Unchanged here; its only untested
input is GPU-written arguments (item 2 above).

## 5. Still unknown

- Which of the two features G13 rejects (one hardware run of each change
  alone would bisect it; not needed to fix RC63G).
- Whether GravityMark's stream hang is this defect. With the old stream every
  asteroid record read instance data from record 0's base; the prolog's
  attribute clamps (`agx_nir_lower_vbo.c`) bound those reads, so an
  out-of-range VS read is not established. The device removal after 31 s
  (`gm1987b-err.txt`) remains unexplained offline.
