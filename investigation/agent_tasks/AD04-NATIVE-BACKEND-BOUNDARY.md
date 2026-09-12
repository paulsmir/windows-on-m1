# ARCHITECT_DECISION — real Asahi Windows-native backend boundary

Draft superseded in sequencing by AD04-ASTRA-V2-PROVENANCE-REVIEW.md.
The direct DRM dependencies below are source facts, but the generic new-device
API is not the approved next implementation step. Correct existing v2/capture
contracts and connect a concrete native caller first.

## Evidence

Pinned Mesa `9aa1215f878b504f66159dd2ead4c7973142126e` is MIT-licensed under
the checked source contract. Current source facts are:

- `agx_screen_create` calls `agx_open_device` and directly creates a DRM
  syncobj. `agx_create_context` directly creates two more syncobjs.
- `agx_open_device` does a DRM version probe, obtains global parameters and
  creates a DRM VM/queue before ordinary `agx_device_ops` calls.
- `agx_batch.c` uses syncobj create/wait/import/export/destroy around its
  `dev->ops.submit` call. `agx_device_ops` alone does not abstract that work.
- Existing `AGX_WIN32_PIPE_SCREEN` is a deliberately limited Gallium wrapper.
  It is not an `agx_screen`, an `agx_context`, or a legitimate substitute for
  one.

## Rejected path

No fake DRM fd, no no-op `drmSyncobj*` shim, no fake successful queue or VM
operation, and no routing through a software Gallium screen. Those would make
the native source appear initialized without giving the existing physical
Windows Render/Patch/Submit path ownership of allocation, fence or reset.

## Selected staged architecture

`AGX_WIN32_NATIVE_DEVICE` is device-scoped and borrowed by the native Mesa
objects. It supplies real Windows callbacks for:

1. initial immutable hardware parameters and class allocation;
2. BO create/map/free and a typed token/serial/generation identity;
3. physical patch-list bind/unbind and submission through the existing KMD
   transport; and
4. fence create/wait/retire and reset-generation invalidation.

It must be an explicit replacement backend, not an adapter pretending that a
Windows handle is a Linux DRM fd. The existing `agx_device_ops_t` remains a
useful inner seam, but a small native-source overlay must additionally replace
the direct syncobj calls in `agx_pipe.c` and `agx_batch.c`.

## Ownership

| Layer | Owns |
| --- | --- |
| Windows/VidMm | allocation lifetime, residency and physical placement |
| KMD | physical mapping, patching, submission and Windows fences |
| Native backend | BO token/serial projection, callback validation, no PA disclosure |
| Mesa `agx_screen/context` | state/command construction and borrowed BO refs |
| Typed capture | request-scoped retained source references until exact fence retire |

BO retain is not residency. A completion fence is not a display-retirement
fence. The native backend never exposes retained-root/private mappings.

## First executable migration slice

Implement the Windows-native device/backend API and deterministic lifecycle
test before touching `agx_screen_create`:

`open(owner,generation,callbacks) -> query immutable parameters -> create BO
identity -> map bounded CPU range -> typed capture reference -> submit/fence
receipt -> retire/invalidate -> reject stale operations`.

Then add a small, license-reviewed source overlay that creates a real
`agx_screen` from this backend and calls the same real context initializer with
Windows fence callbacks. Do not advertise a pipeline or run hardware until
that source slice builds, links and executes a native shader/command fixture.
