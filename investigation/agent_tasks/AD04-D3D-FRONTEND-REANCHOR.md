# ARCHITECT_DECISION — D3D frontend is later; WGL/OpenGL is first production seam

## Evidence

Pinned current production `render-admission/umd/src/umd.c` zeroes
`D3DWDDM1_3DDI_DEVICEFUNCS` then publishes only resource lifetime, format,
flush, destruction and direct-flip callbacks. It publishes no ordinary
state/shader/draw D3D callbacks. `AdmissionUmdSubmitClear` creates a fixed
clear ABI command directly and calls `pfnRenderCb`; it is not a dynamic D3D
producer. `AdmissionUmdDescribePrimary` accepts only the 2560x1600 primary
BGRA8 present resource.

The C++ D3D wrapper and pipe device compiled in AD04 are controlled ownership
tests. They are not wired into the exported production UMD DDI table.

## Corrected decision

The approved Full Graphics design at
`docs/superpowers/specs/2026-09-08-windows-mesa-agx-design.md`, sections 5–6
and ordered checkpoints 1–9, selects Mesa WGL/OpenGL as the first production
frontend. This is the path to the CS 1.6 acceptance target. It explicitly
places Mesa D3D10 UMD integration after the shared transport is stable.

Do not attach the derived Mesa `agx_build_pipeline` overlay to the current
qualification clear/present route. It would create an unadvertised side
producer. Equally, do not expand the hand-written D3D function table before
the WGL transport has an executable hardware renderer path.

The next production migration slice is the WGL transport contract:

```
WGL/OpenGL Mesa frontend
  -> device-scoped real Mesa pipe context
  -> construction graph + typed relocation capture
  -> current Render/Patch/Submit materializer
```

The first implementation sub-slice must be the existing design's fake
user/kernel transport and WGL loader/lifecycle control, then one
Mesa-originated dynamic clear through the real KMD translator. The D3D callback
inventory remains useful later but is not the critical path to CS 1.6.

## Non-decisions

No GPUVA, WDDM caps, KMD scheduler, AGX firmware, DCP, physical mapping or
hardware candidate changes are authorized by this decision.
