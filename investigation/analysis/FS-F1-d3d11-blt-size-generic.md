# FS-F1: size-generic D3D10/11 DXGI Blt

Class C (correctness), FULL GRAPHICS. Offline change: no hardware, guest,
builder or package operation; the MSVC compile gate was not run. Source of
the defect: `.local/night-20261110/fs-windows-audit/REPORT.md` F1 (line 51).
Before this change `_Blt` accepted only the destination rectangle
0,0,2560,1600 with Flags == Present, `Blt1` refused every source
sub-rectangle with E_NOTIMPL, and `AgxD3d10WindowsPresentationBlt` always
blitted a 2560x1600 box, also for ResourceCopy between two presentation
resources of any other size.

## WINDOWS CONTRACT

Pinned header `.local/reference/wdk26100/dxgiddi.h`:

- `:175-182` DXGI_DDI_MODE_ROTATION: UNSPECIFIED 0, IDENTITY 1, ROTATE90 2,
  ROTATE180 3, ROTATE270 4.
- `:228-242` DXGI_DDI_ARG_BLT_FLAGS: Resolve 0x1, Convert 0x2, Stretch 0x4,
  Present 0x8, 28 reserved bits.
- `:244-257` DXGI_DDI_ARG_BLT: hDevice, hDstResource, DstSubresource,
  DstLeft/DstTop/DstRight/DstBottom (UINT), hSrcResource, SrcSubresource,
  Flags, Rotate.
- `:266-283` DXGI_DDI_ARG_BLT1: the same plus SrcLeft/SrcTop/SrcRight/
  SrcBottom (UINT).

Microsoft Learn:
[DXGI_DDI_BASE_FUNCTIONS, pfnBlt](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dxgiddi/ns-dxgiddi-dxgi_ddi_base_functions),
[DXGI_DDI_ARG_BLT](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dxgiddi/ns-dxgiddi-dxgi_ddi_arg_blt),
[DXGI_DDI_ARG_BLT_FLAGS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dxgiddi/ns-dxgiddi-dxgi_ddi_arg_blt_flags),
[DXGI_DDI_ARG_BLT1](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dxgiddi/ns-dxgiddi-dxgi_ddi_arg_blt1).

1. Dst Left/Top is the upper-left corner and Right/Bottom the lower-right
   corner of the destination rectangle. Nothing ties it to the panel size.
2. BltDXGI "always uses a whole source subresource". Blt1 carries a source
   rectangle, "typically a dirty subrectangle".
3. One call may require resolve, colour-format conversion and stretch or
   shrink "all at once". With Present set the driver must not resolve into
   the destination and then convert in place.
4. A stretch or shrink "must be as good as the stretch or shrink that a
   bilinear filter performs".
5. With Resolve set "the source is a multi-sampled resource".
6. The runtime never passes Flags 0 together with ROTATION_IDENTITY; it calls
   ResourceCopy or ResourceCopyRegion for a plain copy.
7. If the driver never answers DXGI_DDI_ERR_UNSUPPORTED from CreateResource,
   the runtime never passes ROTATE90/180/270, and the driver is "not
   required to support rotation".
8. sRGB source to non-sRGB destination: copy the content unchanged.
9. Source: TEXTURE2D with D3D10_DDI_BIND_PRESENT in a display-mode format.
   Destination: TEXTURE2D bound as render target in a display-mode format.
10. Stereo back buffers (ArraySize 2) require in-range non-zero
    Dst/SrcSubresource values.

## AGX/ASAHI CONTRACT

There is no AGX-specific protocol here: the blit is an ordinary Gallium
`pipe->blit` between two imported linear 32-bpp colour resources.

- Reference `src/gallium/drivers/asahi/agx_blit.c:519-557` (`agx_blit`):
  legalizes compression, takes the compute blit only when
  `asahi_compute_blit_supported`, otherwise requires
  `util_blitter_is_blit_supported` (else `UNREACHABLE("Unsupported blit")`),
  flushes the writer of the destination and calls `util_blitter_blit`.
- This project disables the compute blit on Windows:
  `drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py:981`
  (`!agx_device(pipe->screen)->windows_private &&`). Every Windows blit is a
  u_blitter draw.
- Reference `src/gallium/auxiliary/util/u_blitter.c`:
  - `:1708-1772`, `:1783-1790` blit support: destination format renderable,
    source format samplable.
  - `:2300-2338` `util_blitter_blit`: the destination surface uses
    `info->dst.format`, the sampler view `info->src.format`. Format
    conversion is one sample-and-render pass.
  - `:2119-2124` `is_scaled` is derived from the two boxes; an unscaled blit
    is forced to NEAREST. `:2206` a scaled colour blit with
    PIPE_TEX_FILTER_LINEAR uses the linear sampler.
  - `:1008-1040` a multisampled source with a single-sample destination is
    resolved in the fragment shader.
- Reference `src/util/box.h:10-21` `struct pipe_box`: x, width, y, height
  are int32_t.
- Presentation resources (`drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp`):
  `:393` the render resource is imported with the allocation's own
  Width/Height; `:328-338` only D3DDDIFMT_A8R8G8B8 and A8B8G8R8 are
  imported, as linear BGRA8/RGBA8 UNORM; `:340-349` only those two pipe
  formats may be blitted. A DXGI sRGB presentable is stored as linear UNORM,
  so the blit copies its bytes unchanged (item 8).
- Admission (`drivers/apple-agx/render-admission/umd/src/umd.c`):
  `:161-162` at most 8192x8192; `:164-169` a primary must be one of the
  offered fixed-panel source modes; `:184-185` MipLevels 1, ArraySize 1,
  SampleDesc.Count 1. A multisampled or stereo presentable is never created.
- The projected CreateResource never answers DXGI_DDI_ERR_UNSUPPORTED (the
  only occurrence in the frontend Resource.cpp is inside `#if 0`), so item 7
  applies.

## TRANSLATION

Owner: D3D10/11 UMD. Frontend text in
`drivers/apple-agx/mesa/scripts/build-native-asahi-state.py`
(`AgxDxgiBltRect`, `_Blt`), Blt1 shim in
`drivers/apple-agx/mesa/d3d11/agx_wddm2_ddi.cpp`, blit in
`drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp`.

| DXGI input | Result |
| --- | --- |
| NULL args, reserved flag bits, non-zero Dst/SrcSubresource | E_INVALIDARG (unchanged; one subresource exists) |
| foreign device, non-presentation resource, destination == source | E_INVALIDARG (unchanged) |
| Flags 0 | E_NOTIMPL (unchanged; item 6) |
| Resolve 0x1 | E_NOTIMPL, fail-closed: no multisampled presentable exists (F3 scope) |
| Rotate other than IDENTITY | E_NOTIMPL (unchanged; item 7) |
| Convert 0x2, Stretch 0x4, Present 0x8, any combination | one `pipe->blit` |
| Dst rectangle | box x=Left, y=Top, width=Right-Left, height=Bottom-Top on the destination render resource; empty, inverted or outside `width0 x height0` is E_INVALIDARG |
| Blt source | whole source render resource (`width0 x height0`) |
| Blt1 source rectangle | the same derivation and check against the source render resource |
| box sizes equal | PIPE_TEX_FILTER_NEAREST (1:1 copy) |
| box sizes differ | PIPE_TEX_FILTER_LINEAR (item 4) |
| ResourceCopy between two presentation resources | whole to whole (ResourceCopy has already required equal sizes of the two frontend resources) |

Details:

- `presentation_blt_box` (winsys) is the only place that turns a rectangle
  into a box. It does not clamp. `tests/test_d3d11_dxgi_blt_size_generic.py`
  compiles it from the shipped source.
- Convert needs no separate pass: each side of the blit already uses its own
  resource format, so RGBA8 to BGRA8 is the single u_blitter pass required
  by item 3. This path existed before for equal sizes
  (`drivers/apple-agx/render-admission/umd/tests/umd_contract_windows.c:1818-1831`).
- Scaling is decided by the rectangles, not by the Stretch bit. With equal
  rectangles the Stretch bit changes nothing; with different rectangles the
  blit is filtered whether or not the bit is set.
- `_Blt` and `Blt1` share `AgxDxgiBltRect`. The existing entry/return
  measurement and the `reject-BltDXGI` receipt (11 values) moved to it
  unchanged. A failed Blt1 adds `reject-dxgi-blt1-source` with the four
  source values. `reject-dxgi-blt1-subrect` no longer exists.
- No capability bit, format admission or KMD behaviour changed.

Layer ownership: initialization, interrupts, DMA, power and recovery are not
touched. Runtime operation is one more general blit in the UMD context that
already issued the fixed-size blit.

## WHAT IS STILL UNKNOWN

1. Whether DXGI on this driver uses Blt to the primary or flips the back
   buffer for a DISCARD swap chain with BufferCount 1 versus 2 (audit F1
   unknown 1). It decides whether this path or F3 admission is reached
   first.
2. Whether DXGI sends Convert for an RGBA back buffer on a BGRA mode (audit
   F1 unknown 2). The translation is the same either way.
3. HYPOTHESIS: the runtime never sends different source and destination
   sizes without Stretch. If it does, this driver scales instead of
   rejecting.
4. HYPOTHESIS: Flags without Present (for example Stretch alone) need no
   handling different from Present. The driver flushes in both cases.
5. Actual hardware behaviour of a scaled u_blitter draw between two
   presentation resources on the Windows path. The fixed-size 1:1 blit
   used the same u_blitter path; the linear-sampler variant was not run on
   hardware.
6. The copy direction non-sRGB source to sRGB destination is not covered by
   the Microsoft note (item 8). Bytes are copied unchanged in both
   directions.
7. The MSVC /W4 /WX build of the three C++ sources was not run.

## Checkpoint

Smallest hardware check (audit REPORT.md:60 and probe table :119): probe
step `AGX_FS_D3D11_BLT` at 1920x1080 BGRA8 on the scaled m1n1, DISCARD swap
chain, `ResizeTarget`, `SetFullscreenState(TRUE)`, `ResizeBuffers`, 120
presents. The probe is specified in the audit only; it is not in the
repository yet.

- Expected PASS line: `SetFullscreenState` S_OK and 120 presents S_OK;
  `Wom1DisplayModesReceipt` CommittedWidth/Height = 1920x1080; vUART line
  `display: scanout source 1920x1080 ...`; the UMD log has no
  `reject-BltDXGI` and no `reject-dxgi-blt1-source` row.
- Failure criterion: any `reject-BltDXGI` or `reject-dxgi-blt1-source` row
  (its values name the rectangle, Flags and Rotate that were refused), a
  failing present HRESULT, a committed size other than 1920x1080, or a
  device loss during the blit.
- If the present measurement records no native Blt entry (edge
  `AdmissionUmdMeasureNativeBltEntry`) and the log has no Blt reject row,
  DXGI flipped instead of blitting: the run says nothing about F1 and
  answers unknown 1.
- Follow-up step `AGX_FS_D3D11_BLT_MSAA` (REPORT.md:120) is expected to
  stop earlier, at presentable admission (SampleDesc.Count 4), not in Blt.
- Recovery: normal probe exit restores the mode. The change is UMD-only, so
  rollback is the previous UMD package.
