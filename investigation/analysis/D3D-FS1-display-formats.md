# D3D-FS1: fullscreen swap-chain formats (R8G8B8A8 display modes)

Status: design. Origin: EXP1876G run 2. Night Raid in fullscreen failed with
"Window/swapchain initialization failed: Display mode list not found for given
format".

## Evidence

- **EXP1878 (DXGI mode lists on the AppleAgx output).** Only `B8G8R8A8_UNORM`
  and `B8G8R8A8_UNORM_SRGB` have a mode (2560x1600, 60 Hz). `R8G8B8A8_UNORM(_SRGB)`,
  `B8G8R8X8`, `R10G10B10A2` and FP16 lists are empty.
- **Recovery profile.** With the inbox Basic Display driver on the same panel,
  DXGI lists 8888, 1010102 and FP16 modes.
- **The UMD is not involved.** With `APPLE_AGX_D3D_MSAA_CAPS=40` and without it,
  the lists are identical and no UMD trace is written. `GetDisplayModeList` does
  not load the UMD; the lists come from the KMD's VidPN source modes.
- **Source code.**
  - `display.c` EnumVidPnCofuncModality publishes one source mode,
    `D3DDDIFMT_A8R8G8B8`, and CommitVidPn refuses any other format.
  - `AppleAgxFixedPanelCommit`, the m1n1 scanout broker
    (`hv_agx_scanout_broker.c`, `hv_agx_power_mmio.c`) and
    `dcp_iomfb_present.c` (`surface->format = FORMAT_BGRA`) carry only
    BGRA8888.

## WINDOWS CONTRACT

- DXGI builds an output's mode list from the source modes the KMD reports for
  the path (D3DKMDT_GRAPHICS_RENDERING_FORMAT.PixelFormat). The D3DDDI formats
  map to DXGI formats as follows:

  | D3DDDI format | DXGI format(s) |
  |---|---|
  | A8R8G8B8 | B8G8R8A8_UNORM(_SRGB) |
  | A8B8G8R8 | R8G8B8A8_UNORM(_SRGB) |
  | A2B10G10R10 | R10G10B10A2_UNORM |
  | A16B16G16R16F | R16G16B16A16_FLOAT |

- Flip-model swap chains may use R16G16B16A16_FLOAT, B8G8R8A8_UNORM or
  R8G8B8A8_UNORM as the mode format (DXGI_MODE_DESC remarks).
- A fullscreen swap chain may make DXGI commit a VidPN whose source mode has
  the swap chain's format. CommitVidPn must then accept it, and the
  primary/flip path must scan it out with the same channel order.

## AGX/ASAHI CONTRACT

Asahi `drivers/gpu/drm/apple/plane.c` `drm_format_to_dcp`:

| DRM format | DCP format | DXGI equivalent |
|---|---|---|
| XRGB/ARGB8888 | `BGRA` | B8G8R8A8 |
| XBGR/ABGR8888 | `RGBA` | R8G8B8A8 |
| XRGB/ARGB2101010 | `L10R` | B10G10R10A2 = D3DDDIFMT_A2R10G10B10, *not* DXGI R10G10B10A2 |

- The primary plane lists both 8888 orders.
- DCP has no FP16 primary format in that table.

## TRANSLATION

### Step 1 (this item): A8B8G8R8 alongside A8R8G8B8

**KMD (`display.c`)**
- EnumVidPnCofuncModality adds a second source mode with the same size and
  stride and `D3DDDIFMT_A8B8G8R8`.
- CommitVidPn accepts either format and records CommittedFormat.

**Shared panel and scanout ABI**
- Add `APPLE_AGX_SCANOUT_FORMAT_RGBA8888 = 2`.
- `AppleAgxFixedPanelCommit` accepts both.
- Each present/flip carries the allocation's format.
- The HV scanout broker and power-MMIO request accept 1 or 2.

**m1n1 `dcp_iomfb_present.c`**
- `surface->format` is `BGRA` (0x42475241) or `RGBA` (0x52474241) per request:
  Asahi `iomfb_plane.h` defines `DCP_FORMAT_BGRA fourcc_code('A','R','G','B')` (the existing m1n1
  value) and `DCP_FORMAT_RGBA fourcc_code('A','B','G','R')`.

**KMD allocation and present checks**
- `allocation_windows.c` (primary/shadow) and `scanout_windows.c` (`format ==
  A8R8G8B8`) admit A8B8G8R8 primaries.
- The DWM shadow stays A8R8G8B8.

**Not in this step**
- 1010102: needs the A2R10G10B10 ↔ DXGI R10G10B10A2 channel-order question
  answered first, because DCP `L10R` is the BGR10 order.
- FP16: no DCP primary format; it would need a conversion copy.

## WHAT IS STILL UNKNOWN

1. Whether DXGI actually commits an A8B8G8R8 mode for an R8G8B8A8 fullscreen flip
   swap chain on Windows 11 26100, or keeps the A8R8G8B8 desktop mode and
   composes. Either way the mode list must contain the format.
2. (resolved) DCP `RGBA` = fourcc('A','B','G','R') = 0x52474241 (Asahi iomfb_plane.h).
3. Whether DWM tolerates two source formats in the cofunctional set
   (EnumVidPnCofuncModality pivots).

## Checkpoints

1. **Offline.** Unit tests for the fixed-panel format admission and the HV broker
   request validation (both formats, others refused).
2. **Hardware, one variable.** The KMD publishes A8B8G8R8 too:
   - `agx_dxgi_modes_probe` lists R8G8B8A8 modes;
   - the desktop is unchanged (DWM stays A8R8G8B8);
   - Night Raid fullscreen GT1 either runs, or a commit receipt shows the
     A8B8G8R8 commit attempt.
3. **Hardware.** RGBA scanout through DCP: a fullscreen R8G8B8A8 probe draws a
   known colour; the screenshot and the scanout readback show the right channel
   order.

Recovery: the ordinary GPU-visible recovery (`recXXXXh/recover.sh`).
