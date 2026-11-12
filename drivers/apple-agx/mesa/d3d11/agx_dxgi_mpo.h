/*
 * S2 (second display plane): the DXGI multiplane-overlay answers of the
 * D3D11 user-mode driver (agx_wddm2_ddi.cpp) as pure functions, so a host
 * test can check them (tests/test_wddm2_mpo_planes.py).
 *
 * One predicate decides everything: "the kernel reports two overlay
 * planes".  Its only source is the kernel: AGX_WIN32_DEVICE_FEATURE_MPO_TWO_PLANES
 * in the adapter private data (apple_agx_win32_device_info.h), which the KMD
 * sets only while Wom1MpoTwoPlanes is effective, and then only for
 * VidPnSource 0 (the only source the KMD accepts).  agx_d3d10_windows.cpp
 * reads it.  Predicate FALSE: every answer is the one this driver gave
 * before S2 (MaxPlanes 1, no capability group, no colour space, the
 * present entries E_NOTIMPL).
 *
 * Predicate TRUE (S2 specification v1, OV-ARCHITECT-HANDOFF.md 2026-11-11):
 * MaxPlanes 2 including DWM's primary (DXGI_DDI_MULTIPLANE_OVERLAY_CAPS),
 * one capability group of two RGB planes with no scaling: stretch and
 * shrink factor 1.0, OverlayCaps = DXGI_DDI_MULTIPLANE_OVERLAY_FEATURE_CAPS_RGB
 * only, StereoCaps 0 ("Reserved for system use. Set this value to zero.",
 * DXGI_DDI_MULTIPLANE_OVERLAY_GROUP_CAPS); colour space only 8-bit RGB SDR
 * sRGB (the KMD rule table mpo_contract.h accepts the BGRA8 and RGBA8
 * classes in SDR sRGB only).
 *
 * Neutral types only, so the same header compiles in C and C++ and on the
 * host.  The functions that read or write the DXGI structures are compiled
 * only when the includer has the pinned WDK declarations and defines
 * AGX_DXGI_MPO_DDI_TYPES.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AGX_DXGI_MPO_H
#define AGX_DXGI_MPO_H

#include <string.h>

/* MaxPlanes of the two-plane answer, including DWM's primary. */
#define AGX_DXGI_MPO_PLANES 2u
/* DXGI_DDI_MULTIPLANE_OVERLAY_FEATURE_CAPS_RGB (dxgiddi.h). */
#define AGX_DXGI_MPO_FEATURE_CAPS_RGB 0x20u
/* DXGI_FORMAT_R8G8B8A8_UNORM / DXGI_FORMAT_B8G8R8A8_UNORM (dxgiformat.h). */
#define AGX_DXGI_MPO_FORMAT_R8G8B8A8_UNORM 28u
#define AGX_DXGI_MPO_FORMAT_B8G8R8A8_UNORM 87u
/* D3DDDI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 (d3dukmdt.h): sRGB, SDR. */
#define AGX_DXGI_MPO_COLOR_SPACE_SRGB 0u

/* Why a PresentMultiplaneOverlay plane list is refused (receipt value). */
#define AgxDxgiMpoPlanesOk 0u
#define AgxDxgiMpoPlanesArgument 1u    /* no plane array */
#define AgxDxgiMpoPlanesCount 2u       /* 0 planes or more than MaxPlanes */
#define AgxDxgiMpoPlanesDisabled 3u    /* DP-1: no plane enabled (before DP-1: any plane with Enabled FALSE) */
#define AgxDxgiMpoPlanesResource 4u    /* not a presentation resource of this device */
#define AgxDxgiMpoPlanesSubresource 5u /* SubResourceIndex != 0 */
#define AgxDxgiMpoPlanesLayerIndex 6u  /* LayerIndex not a permutation of 0..n-1 */

typedef struct _AGX_DXGI_MPO_PLANE {
  unsigned int LayerIndex;
  unsigned int Enabled;          /* 0 or nonzero */
  unsigned int HasResource;      /* a presentation resource of this device */
  unsigned int SubResourceIndex;
} AGX_DXGI_MPO_PLANE;

/* AGX_WIN32_DEVICE_FEATURE_MPO_TWO_PLANES (apple_agx_win32_device_info.h). */
#define AGX_DXGI_MPO_FEATURE_TWO_PLANES 0x20u

/* The predicate: the kernel published the two-plane feature bit and the
 * question is about VidPnSource 0.  Anything else is one plane. */
static inline int
AgxDxgiMpoFeatureTwoPlanes(unsigned int Features, unsigned int VidPnSourceId)
{
  return (Features & AGX_DXGI_MPO_FEATURE_TWO_PLANES) != 0u && VidPnSourceId == 0u;
}

/* DXGI_DDI_MULTIPLANE_OVERLAY_CAPS: MaxPlanes and NumCapabilityGroups. */
static inline void
AgxDxgiMpoCaps(int TwoPlanes, unsigned int *MaxPlanes, unsigned int *Groups)
{
  *MaxPlanes = TwoPlanes ? AGX_DXGI_MPO_PLANES : 1u;
  *Groups = TwoPlanes ? 1u : 0u;
}

/* DXGI_DDI_MULTIPLANE_OVERLAY_GROUP_CAPS of group GroupIndex; returns 1 when
 * the group exists (the outputs are written only then). */
static inline int
AgxDxgiMpoGroupCaps(int TwoPlanes, unsigned int GroupIndex, unsigned int *NumPlanes,
                    float *MaxStretch, float *MaxShrink, unsigned int *OverlayCaps,
                    unsigned int *StereoCaps)
{
  if (!TwoPlanes || GroupIndex != 0u)
    return 0;
  *NumPlanes = AGX_DXGI_MPO_PLANES;
  *MaxStretch = 1.0f;
  *MaxShrink = 1.0f;
  *OverlayCaps = AGX_DXGI_MPO_FEATURE_CAPS_RGB;
  *StereoCaps = 0u;
  return 1;
}

/* DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYCOLORSPACESUPPORT::Supported. */
static inline int
AgxDxgiMpoColorSpaceSupported(int TwoPlanes, unsigned int Format, unsigned int ColorSpace)
{
  return TwoPlanes && ColorSpace == AGX_DXGI_MPO_COLOR_SPACE_SRGB &&
         (Format == AGX_DXGI_MPO_FORMAT_B8G8R8A8_UNORM ||
          Format == AGX_DXGI_MPO_FORMAT_R8G8B8A8_UNORM);
}

/* The plane list of PresentMultiplaneOverlay(1) this driver can hand to
 * pfnPresentMultiplaneOverlayCb: 1..MaxPlanes planes, each enabled, each one
 * of this device's presentation resources at subresource 0, LayerIndex
 * values "sequential from top to bottom" (DXGI_DDI_PRESENT_MULTIPLANE_OVERLAY),
 * i.e. each of 0..Count-1 exactly once.  Whether the configuration can be
 * shown is the kernel's decision (DxgkDdiCheckMultiPlaneOverlaySupport3);
 * this is only the translation's precondition. */
static inline unsigned int
AgxDxgiMpoCheckPlanes(const AGX_DXGI_MPO_PLANE *Planes, unsigned int Count)
{
  unsigned int seen = 0u, i;
  if (!Planes)
    return AgxDxgiMpoPlanesArgument;
  if (Count == 0u || Count > AGX_DXGI_MPO_PLANES)
    return AgxDxgiMpoPlanesCount;
  /* DP-1 (EXP2363): a listed plane with Enabled FALSE is a legal request
   * ("turn this layer off in this present", DXGI_DDI_PRESENT_MULTIPLANE_OVERLAY
   * .Enabled, dxgiddi.h); DWM sends it when the plane set changes.  Its
   * resource, ownership and subresource are not validated (Microsoft gives
   * no meaning to them for a disabled plane); LayerIndex uniqueness holds for
   * every listed plane; at least one plane must be enabled. */
  unsigned int enabled = 0u;
  for (i = 0u; i < Count; ++i) {
    if (Planes[i].LayerIndex >= Count || (seen & (1u << Planes[i].LayerIndex)) != 0u)
      return AgxDxgiMpoPlanesLayerIndex;
    seen |= 1u << Planes[i].LayerIndex;
    if (!Planes[i].Enabled)
      continue;
    ++enabled;
    if (!Planes[i].HasResource)
      return AgxDxgiMpoPlanesResource;
    if (Planes[i].SubResourceIndex != 0u)
      return AgxDxgiMpoPlanesSubresource;
  }
  return enabled != 0u ? AgxDxgiMpoPlanesOk : AgxDxgiMpoPlanesDisabled;
}

/* The neutral view of one DXGI plane (DXGI_DDI_PRESENT_MULTIPLANE_OVERLAY and
 * ..._OVERLAY1 carry the same LayerIndex, Enabled, hResource and
 * SubResourceIndex members, dxgiddi.h). */
static inline AGX_DXGI_MPO_PLANE
AgxDxgiMpoPlane(unsigned int LayerIndex, int Enabled, int HasResource,
                unsigned int SubResourceIndex)
{
  AGX_DXGI_MPO_PLANE plane;
  plane.LayerIndex = LayerIndex;
  plane.Enabled = Enabled ? 1u : 0u;
  plane.HasResource = HasResource ? 1u : 0u;
  plane.SubResourceIndex = SubResourceIndex;
  return plane;
}

#ifdef AGX_DXGI_MPO_DDI_TYPES
static inline void
AgxDxgiMpoFillCaps(int TwoPlanes, DXGI_DDI_MULTIPLANE_OVERLAY_CAPS *Caps)
{
  UINT planes = 0u, groups = 0u;
  AgxDxgiMpoCaps(TwoPlanes, &planes, &groups);
  Caps->MaxPlanes = planes;
  Caps->NumCapabilityGroups = groups;
}

static inline int
AgxDxgiMpoFillGroupCaps(int TwoPlanes, UINT GroupIndex,
                        DXGI_DDI_MULTIPLANE_OVERLAY_GROUP_CAPS *Group)
{
  UINT planes = 0u, overlay = 0u, stereo = 0u;
  float stretch = 0.0f, shrink = 0.0f;
  if (!AgxDxgiMpoGroupCaps(TwoPlanes, GroupIndex, &planes, &stretch, &shrink, &overlay,
                           &stereo))
    return 0;
  Group->NumPlanes = planes;
  Group->MaxStretchFactor = stretch;
  Group->MaxShrinkFactor = shrink;
  Group->OverlayCaps = overlay;
  Group->StereoCaps = stereo;
  return 1;
}

/* DXGIDDICB_PRESENT_MULTIPLANE_OVERLAY (dxgiddi.h; Microsoft Learn):
 * pDXGIContext from the DDI argument unchanged, hContext the context the
 * driver created with pfnCreateContextCb, no broadcast (BroadcastContextCount
 * 0: broadcasting needs MMIO flips to several contexts, not used), and one
 * AllocationInfo per plane in the DDI's pPresentPlanes order: the plane
 * resource's kernel allocation handle and its SubResourceIndex.  Count must
 * have passed AgxDxgiMpoCheckPlanes (Count <= 2 <= 16 =
 * DXGI_DDI_MAX_MULTIPLANE_OVERLAY_ALLOCATIONS). */
static inline void
AgxDxgiMpoFillPresentCb(DXGIDDICB_PRESENT_MULTIPLANE_OVERLAY *Cb, void *DxgiContext,
                        HANDLE Context, const D3DKMT_HANDLE *Allocations,
                        const AGX_DXGI_MPO_PLANE *Planes, UINT Count)
{
  UINT i;
  memset(Cb, 0, sizeof(*Cb));
  Cb->pDXGIContext = DxgiContext;
  Cb->hContext = Context;
  Cb->BroadcastContextCount = 0u;
  Cb->AllocationInfoCount = Count;
  /* One entry per DDI plane in the DDI's order (AllocationInfoCount =
   * plane count).  DP-1: a disabled plane keeps its entry with
   * PresentAllocation 0 and SubResourceIndex 0 (dxgkrnl maps allocation 0
   * of a disabled plane to "no allocation"); it is never dropped and never
   * given another plane's allocation. */
  for (i = 0u; i < Count; ++i) {
    Cb->AllocationInfo[i].PresentAllocation = Planes[i].Enabled ? Allocations[i] : 0u;
    Cb->AllocationInfo[i].SubResourceIndex = Planes[i].Enabled ? Planes[i].SubResourceIndex : 0u;
  }
}
#endif /* AGX_DXGI_MPO_DDI_TYPES */

#endif /* AGX_DXGI_MPO_H */
