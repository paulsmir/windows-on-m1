# D3D-H7: the WDDM 2.0 D3D11 DDI (2026-10-15)

## Evidence

EXP1299 F receipts: the D3D11 runtime opens the adapter with
`Interface 0x000b0011` = `(11 << 16) | 17` = `D3DWDDM2_0_DDI_INTERFACE_VERSION`
(d3d10umddi.h, header minor 5 definition), `Version 0x00060000`, queries
`GetCaps` 0x80/0x81/0x82 and `GetSupportedVersions` (D3D10_0, D3D10_0_x,
D3D11_0), and fails a FL 11_0 device with `0x887a0020` without calling
`CreateDevice`. FL 10_x devices keep working on the 10.0 DDI.

## WINDOWS CONTRACT

- `D3D10DDIARG_CREATEDEVICE`: when `Interface == D3DWDDM2_0_DDI_INTERFACE_VERSION`
  the driver fills `pWDDM2_0DeviceFuncs` (`D3DWDDM2_0DDI_DEVICEFUNCS`), and the
  runtime passes `pWDDM2_0UMCallbacks` (prefix-compatible with the 10.0/11.0
  core-layer callbacks) and `ppfnRetrieveSubObject`.
- `IS_DXGI1_4_BASE_FUNCTIONS(Interface, Version)` holds for that interface:
  the driver fills `DXGI1_4_DDI_BASE_FUNCTIONS` (Blt1, Offer/Reclaim, MPO
  queries, Present1, CheckPresentDurationSupport, TrimResidencySet, MPO
  colour space, PresentMultiplaneOverlay1).
- The 11.1 runtime exposes constant-buffer offsetting, `ClearView`, no-overwrite
  maps of dynamic constant buffers, copy/update flags and overlapping copies
  (`CopySubresourceRegion1`) without a driver cap; the driver caps of 11.1 to
  WDDM 2.0 cover only logic ops, debug binaries, the architecture (TBDR), min
  precision, tiled resources, markers, conservative rasterization, VP/RT
  array index from any stage, memory architecture, texture layouts and GPU VA
  bits.

## AGX/ASAHI CONTRACT

- Asahi binds constant buffers with an offset (`constant_buffer_offset_alignment
  = 16`, agx_pipe.c:2108); the robust contexts return 0 past a buffer's end.
- AGX is a tile-based deferred renderer on unified memory; GPU VA is 39 bits
  (apple_agx_gpuva_g3_caps.h).
- No tiled resources, logic ops in the output merger, conservative
  rasterization, or planar formats are implemented.

## TRANSLATION

`drivers/apple-agx/mesa/d3d11/agx_wddm2_ddi.cpp`, header minor 9+ layout
(`static_assert`): the 152-entry prefix is the D3D11 table; 34 retyped
entries are adapters onto the 11.0 entries (11.1 constant-buffer windows
through `AgxSetConstantBuffersRange`, 11.1 blend/signature structures,
WDDM 2.0 rasterizer/views/queries/flush, WDDM 1.3 multisample flags);
18 additions are implemented (Discard, ClearView RTV/UAV, no-overwrite
constant-buffer map, direct-flip refusal, shader comments) or fail closed
behind absent caps (tiled resources, markers, protection, texture layouts).
The DXGI 1.2-1.4 entries map single-surface Present1 and whole-source Blt1
onto the DXGI 1.1 entries, keep offered resources, and report no MPO.
Advertised only with the FL 11_0 switch (`APPLE_AGX_D3D11_FL11=1`;
`APPLE_AGX_D3D11_WDDM2=0` opts out).

`tests/test_wddm2_ddi_table.py` derives the retyped and new slots from the
pinned header and fails on any slot left with its 11.0 pointer.

## WHAT IS STILL UNKNOWN

- Whether the runtime accepts `D3DWDDM2_0_DDI_SUPPORTED` (header minor 9+
  encoding `11.32`, build 6) for the `11.17` interface it opened with, and
  which caps it then queries (receipts: measure-getcaps).
- Whether it calls `RetrieveSubObject` at device creation (receipt).
- Whether the FL 11_0 checkpoints (SM 5 stages, UAVs, tessellation) pass once
  the device exists.
