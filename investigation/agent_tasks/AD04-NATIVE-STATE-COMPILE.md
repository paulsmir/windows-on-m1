# Native Asahi state compilation — 2026-09-13

The actual pinned agx_state.c (including agx_build_pipeline/agx_draw_vbo) and
pool.c now compile to Windows x64 and ARM64 objects. No replacement producer
was written. There is no native draw execution or hardware PASS yet.

Source: Mesa 9aa1215f878b504f66159dd2ead4c7973142126e, source-state SHA256
5015b75863202a170f8d6015eb82a76a70ecd4b92204894fa3d1886b1baa94ba.
The build recipe uses the already-proven NIR compiler flags and canonical
generated agx_pack/libagx headers. It does not rebuild the closed full compiler.

## Tier-A portability decisions

Only derived build-local copies change. Source MIT notices in Asahi and the
permissive BSD/MIT notice in drm.h are preserved; source reference is untouched.

1. Native state has no libdrm calls; remove its unused xf86drm include dependency
   for Windows. In the OS-private device header, vdrm/device pointers remain
   opaque; no Linux ioctl constants or substitute successful APIs are supplied.
   The Windows device backend must implement its real allocation/submit methods.
2. Windows OS-private bo_map_lock uses Mesa's mtx_t rather than pthread_mutex_t.
   This is not a firmware/wire-layout claim. All Windows native units must use
   the same derived header and initialize/use the corresponding Windows lock.
3. agx_fs_epilog_link_info: native is 3 octets followed by 8 flag bits, size and
   alignment 4. Microsoft bitfield grouping produced size16. Use uint8_t for
   the seven unsigned one-bit fields and explicit alignment4, keeping the bool
   field boolean. Native and Windows executable probes test all256 flag patterns,
   the three fixed octets, sizeof/alignment and boolean normalization from2.
   The original sizeof4 assertion remains and all later state assertions pass.
4. Meson flags originated from MSVC; clang-cl supports packed attributes.
   Set HAVE_FUNC_ATTRIBUTE_PACKED for this compiler so source PACKED contracts
   work, including poly_geometry_params size316. No assertion is disabled.
5. Canonical libagx_helper has zero arguments. MS C cannot represent the GNU
   zero-sized host struct. Keep a host-only placeholder and make BOTH generated
   helper dispatch wrappers pass explicit LIBAGX_HELPER_ARGS_BYTES=0. The zero
   wire-payload assertion replaces the inapplicable zero host-size assertion.
   All nonempty generated argument layouts/assertions remain unchanged. An
   executable test calls both actual generated wrappers, verifies zero transmitted
   bytes and exactly-once evaluation of the struct argument. The failed
   zero-length-array experiment is preserved and is not part of this projection.
6. libagx_dgc's local uint spelling depended on another include in state.c.
   Use uint32_t for that one index-buffer byte-range local so pool.c also
   compiles with its own real include chain. Arithmetic and widths are unchanged.

The initial generated helper test incorrectly forced aligned4 onto all generated
records; this failed the copy-query size50 assertion. The final test uses the
actual generated-header include contract instead and all assertions pass.

## Evidence and scope

Builder: FRYZZING clang-cl LLVM20 with pinned WDK26100/MSVC14.44 environment.
Recipe: drivers/apple-agx/mesa/scripts/build-native-asahi-state.py.
Each invocation preserves inputs/overlays/command/build logs under a new output.
Windows x64 objects: AD04-native-state-012, plus helper-wrapper execution PASS.
Windows ARM64 objects: AD04-native-state-arm64-001; execution NOT_RUN.
Raw log/manifest copies: investigation/evidence/AD04-native-asahi-state/.
Precompiled generated libagx_shaders.h SHA256:
b10e6ec36950ecd55878f4665f644969a93ba7bdbeb75f7c6584e8a1da11bc37.

## Actual remaining native integration

These are compilation units, not a linked backend or a runtime submission.
Native pool still calls agx_bo_create/unreference/map; those must resolve to
the Windows allocation owner with real NativeBo association and construction
coordinates. agx_build_pipeline currently emits its native address-bearing
graph, but does not yet register all edges in typed capture. Subsequent native
state/resource/scratch references must be captured at creation, snapshotted with
their owners and routed into the accepted request-scoped composer.

No hardware candidate is valid at this point. No synthetic producer, fallback
renderer, fake FD, no-op synchronization, packet success or hardware evidence
is used to bridge the missing native backend. This remains implementable work,
not a fundamental blocker. Keep the Full Graphics objective unchanged.
