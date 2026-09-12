# ARCHITECT_DECISION — real D3D frontend is the first Full Graphics seam

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

## Decision

Do not attach the derived Mesa `agx_build_pipeline` overlay to the current
qualification clear/present route. It would create an unadvertised side
producer and could not satisfy standard D3D/DXGI semantics.

The next production migration slice is a Windows D3D frontend contract:

```
D3D runtime state/shader/resource callback
  -> device-scoped real Mesa pipe context
  -> construction graph + typed relocation capture
  -> current Render/Patch/Submit materializer
```

The first implementation sub-slice must be one supported dynamic resource
type and one state/shader/draw callback chain. It must reuse the existing
device owner, not expose construction coordinates, retain the current direct
flip qualification route separately, and keep unimplemented callbacks absent
rather than returning fake success.

## Non-decisions

No GPUVA, WDDM caps, KMD scheduler, AGX firmware, DCP, physical mapping or
hardware candidate changes are authorized by this decision.
