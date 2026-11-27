# B23 H1: GPUVA broker hardening in m1n1 (design only)

Status: design note. No code has been written or built, and nothing has run on hardware.

Plan row: B23, finding H1. The input analysis is `investigation/analysis/B23-ANALYSIS.md` §3 on the GPU line's branch
`claude/gpu-perf-parity-aaa98a`, commit f23e44eed.

Sources read:
- m1n1 standing source, el2/levers bf9b7b8f (built as image 9bddd9aa):
  - `src/hv_agx_gpuva_v5.[ch]`, `src/hv_agx_gpuva_v5_mmio.c`;
  - `src/hv_agx_retained_platform.c` (dispatch, `translate_guest`, broker init);
  - `src/hv_agx_retained_backing.c`;
  - `src/hv_agx_local_reserve.[ch]`;
  - `src/hv_agx_power_mmio.c` (`hv_agx_local_public_span`, reserve selection);
  - `tests/hv_agx_gpuva_v5_test.c`.
- KMD on the GPU line's branch:
  - `drivers/apple-agx/shared/include/apple_agx_gpuva_broker_v5.h` (wire ABI 6 and its optional extensions);
  - `shared/src/apple_agx_gpuva_g3_graph.c:640-735` (`TryLeafBackingEx`);
  - `shared/src/apple_agx_gpuva_broker_v5_client.c` (QUERY_CAPS use);
  - `render-admission/include/render_memory.h` (scan-out scratch), `render-admission/src/gpuva_g3_windows.c:348-366`
    (mailbox allocation).

The analysis cites line numbers from m1n1 882985fb. The standing tree bf9b7b8f contains that commit, with +95/-35 lines
in these files, so the line numbers below are re-read at bf9b7b8f.

## 1. Observed contract today (R, bf9b7b8f)

- **Reserve layout.**
  - The local reserve is `[segment 2 | private span 64 MiB | backend arena 8 MiB]`.
  - The KMD keeps the top 128 KiB of segment 2 as scan-out scratch; VidMm is told segment 2 ends below it
    (`render_memory.h`, ADMISSION_SCANOUT_SCRATCH_BYTES).
  - The broker is configured with base = reserve IPA, bytes = segment 2 and private_bytes = 64 MiB:
    `hv_agx_retained_platform.c:388-392` → `hv_agx_local_public_span` (`hv_agx_power_mmio.c:410-424`) →
    `hv_agx_gpuva_v5_configure_local` (`hv_agx_gpuva_v5.c:415-436`).
  - The broker is not told where the backend arena or the scratch begin.
- **Local-page grants** (`register_backing`, `hv_agx_gpuva_v5.c:472-535`, local branch :492-498):
  - accepted only through REGISTER_SHARED_BACKING, with AllocationGeneration == local_base (a constant) and pa == ipa;
  - refused only if this owner already holds the page.
  - So any number of processes may hold any segment-2 page; there is no allocation identity.
  - The KMD always sends this form for local pages: `g3_graph.c:704-716`, where `SharedBackingGeneration` is local_base.
- **Backend arena and scratch.** `translate_guest` (`hv_agx_retained_platform.c:56-75`) accepts any guest-RAM page
  that `hv_agx_retained_backing_allowed` allows (`hv_agx_retained_backing.c:17-39`). The reserve is guest RAM.
  Therefore:
  - a backend-arena page passes the system branch of `register_backing` (:500-534) as an ordinary system backing;
  - it passes `table_add` (:266-301) as a page table;
  - a scratch page passes the local branch.
- **Capability mechanism.**
  - The KMD discovers optional extensions with QUERY_CAPS; a refusal means "absent" (`broker_v5.h`; client.c:160-199).
  - The standing broker implements none of them: the wire layer accepts commands only up to ATTACH_MAILBOX
    (`hv_agx_gpuva_v5_mmio.c:27-28`, `validate_envelope` :214-216), and the dispatch default answers INVALID
    (`hv_agx_retained_platform.c` gpuva_execute).

## 2. Design

### 2a. Exclusive local-page grants per process (opt-in extension, wire ABI stays 6)

- **Capability.** `AGX_GPUVA_V5_CAP_LOCAL_EXCLUSIVE = 8ULL` in QUERY_CAPS Reserved[0].
  - The broker then also implements QUERY_CAPS. Today it answers INVALID; afterwards Reserved[0] = the cap bits this
    build has, and Reserved[1] = 1.
  - The KMD tests the bit only. Reserved[1] is shared by the existing caps and is not a per-cap version.
- **Negotiation.** New command `AGX_GPUVA_V5_NEGOTIATE_LOCAL`.
  - It is broker-wide: all payload fields are zero except Token = CAP_LOCAL_EXCLUSIVE and AllocationGeneration =
    `AGX_GPUVA_V5_LOCAL_ABI` (1).
  - It succeeds only while no local grant exists (otherwise BUSY). On success it sets `local_strict` for the rest of
    the broker epoch; `hv_agx_gpuva_v5_init` clears it at a new epoch.
  - Broker-wide, not per process, because the KMD is the only client. A process that the KMD forgot to switch over
    then shows as OWNERSHIP, never as a silent share.
- **Grant command.** New command `AGX_GPUVA_V5_REGISTER_LOCAL_BACKING`, accepted only after NEGOTIATE_LOCAL:
  - `ProcessId`/`ProcessGeneration`: the owner (as today);
  - `AuxIpa`: the page IPA (pa == ipa, inside segment 2 and below the grantable limit of 2b);
  - `AllocationGeneration`: the allocation identity, nonzero and != local_base. The KMD's per-allocation 64-bit
    identity, which must be stable across processes that open the same shared allocation;
  - `Flags` bit 0: `AGX_GPUVA_V5_LOCAL_SHARED` (1 = shared, 0 = exclusive). Other bits must be zero.
- **Per-page state.** New broker arrays, next to `local_grants`:
  - `uint8_t local_mode[LOCAL_PAGES]`: NONE / LEGACY / EXCLUSIVE / SHARED;
  - `uint8_t local_owners[LOCAL_PAGES]`: owner count, at most HV_AGX_GPUVA_V5_PROCESSES = 128, so it fits in 8 bits;
  - `uint64_t local_identity[LOCAL_PAGES]`.
  - With 65536 pages that is 640 KiB more static broker state; the existing local_grants array is 1 MiB.
- **Rules.**

  | Page mode | REGISTER_LOCAL_BACKING exclusive | REGISTER_LOCAL_BACKING shared (identity X) | legacy REGISTER_SHARED_BACKING (local_base) |
  |---|---|---|---|
  | NONE | → EXCLUSIVE(owner, id) | → SHARED(X) | → LEGACY if not strict; OWNERSHIP if strict |
  | EXCLUSIVE | OWNERSHIP | OWNERSHIP | OWNERSHIP |
  | SHARED(Y) | OWNERSHIP | join if X == Y and not already an owner, else OWNERSHIP | OWNERSHIP |
  | LEGACY | OWNERSHIP | OWNERSHIP | join (today's behaviour), unless strict |

  The same owner registering a page it already holds stays OWNERSHIP, as today. Each grant sets the owner's bit in
  `local_grants` and increments `local_owners`.
- **Revoke.** Existing REVOKE_BACKING. For a strict page, AllocationGeneration must equal `local_identity`, or the
  revoke is STALE.
  - BUSY while referenced, as today (:567-570).
  - The last owner leaving resets the page to NONE, identity 0.
  - `hv_agx_gpuva_v5_destroy` (:850-873) already refuses BUSY while the owner holds local grants, so no page can stay
    stranded in a mode.
- **Failure behaviour.**
  - Every conflict is OWNERSHIP (4) with no state change. That is the existing "acknowledged registration refusal"
    which the KMD turns into "unavailable" or a render failure (`g3_graph.c:717-728`).
  - Malformed requests (nonzero reserved fields, Flags > 1, identity 0 or local_base, not negotiated) are INVALID.
  - Never a silent share.
- **`table_add`.** It refuses a local page whose `local_owners` is nonzero. This replaces today's loop over 128 owner
  bitmaps (:288-292), so the change is a constant-time win.

### 2b. Deny the backend arena and the scan-out scratch on every grant path (applies to every KMD, no opt-in)

- **Bounds.** `configure_local` gets two more inputs: `backend_bytes` and `grantable_local_bytes`.
  - New signature: `hv_agx_gpuva_v5_configure_local(b, base, bytes, private_bytes, backend_bytes,
    grantable_local_bytes)`.
  - The caller derives both from the reserve receipt, with the same arithmetic as `hv_agx_local_scanout_allows`
    (`hv_agx_local_reserve.c:26-37`):
    - backend arena = `[base + bytes + private_bytes, + backend_bytes)`, with backend_bytes =
      HV_AGX_LOCAL_BACKEND_BYTES;
    - `grantable_local_bytes = bytes - scratch_bytes`.
  - scratch_bytes is a new receipt constant `HV_AGX_LOCAL_SCANOUT_SCRATCH_BYTES` (0x20000). It mirrors the KMD's
    ADMISSION_SCANOUT_SCRATCH_BYTES, and a host test pins the two equal.
  - The constants already exist per profile in `hv_agx_local_reserve.h`; there is no T8103 constant in logic, and the
    sizes come from the receipt.
  - `configure_local` validates the bounds: no overflow, scratch < bytes, backend_bytes > 0, page alignment.
- **Enforcement points** (one helper `local_denied(b, ipa, pa)`: true for an arena page, or for a segment-2 page at or
  beyond `grantable_local_bytes`):
  - `register_backing`, every branch: REGISTER_BACKING, REGISTER_SHARED_BACKING, the new REGISTER_LOCAL_BACKING, and a
    future REGISTER_SYSMEM_BACKING. Checked on both the IPA and the translated PA. Result OWNERSHIP.
  - `table_add` (REGISTER_TABLE, CREATE's root, RELOCATE_ROOT, which all use it): OWNERSHIP.
  - `attach_mailbox`: INVALID for an arena page, so no other path can map these pages either.
  - `update_leaf` needs no change: it only maps registered backings.
- **Compatibility.**
  - Not opt-in, because no production path maps these pages (I, analysis §3b).
  - The mailbox is ordinary contiguous Windows memory (`gpuva_g3_windows.c:355`).
  - G3 tables are pool or segment-2 allocations below the scratch; VidMm is told segment 2 ends below the scratch.
  - The context-63 mapping of the backend arena is the KMD's own UAT publication, not the broker's, so it is unchanged
    and outside this fix (analysis §3b; D1 needs its own fix).

## 3. Tests (`tests/hv_agx_gpuva_v5_test.c`, plus `hv_agx_gpuva_v5_mmio_test.c` for the wire)

One function per refusal, in the existing fixture style (`local_bitmap_ownership_and_eviction`, :336-358):
- 2a exclusive:
  - owner B refused on A's exclusive page (OWNERSHIP, state unchanged);
  - A's second registration refused;
  - B joins after A revokes.
- 2a shared:
  - B joins A's shared page with the same identity;
  - B is refused with a different identity;
  - exclusive is refused on a shared page and shared is refused on an exclusive page.
- 2a revoke:
  - wrong identity → STALE;
  - the last owner resets the mode, so a new exclusive owner then succeeds;
  - BUSY while a leaf maps the page (the existing local_leaf_refs path).
- 2a negotiation:
  - NEGOTIATE_LOCAL while a legacy grant exists → BUSY;
  - after negotiation, legacy REGISTER_SHARED_BACKING on a local page → OWNERSHIP;
  - before negotiation, REGISTER_LOCAL_BACKING → INVALID;
  - malformed Flags or identity → INVALID;
  - a new epoch (init) clears strict.
- 2a legacy unchanged: the existing `local_bitmap_ownership_and_eviction` and `local_leaf_reference_count` pass
  unmodified. They are the old-KMD contract.
- 2b:
  - the first and last backend-arena page refused for REGISTER_BACKING, REGISTER_SHARED_BACKING, REGISTER_TABLE,
    CREATE root and ATTACH_MAILBOX;
  - the last grantable segment-2 page accepted and the first scratch page refused (boundary on both sides);
  - the last private-span page and the first arena page told apart;
  - `configure_local` refuses inconsistent bounds.
- Wire (mmio test):
  - QUERY_CAPS returns the bit only in a build with the extension;
  - commands past ATTACH_MAILBOX reach `execute` only when implemented;
  - the old KMD's sequence of commands produces byte-identical responses.
- Existing replays must keep passing: `tests/g3_vidmm_replay*.c`, `tests/g3_system_lifetime_cases.c`,
  `test_g3_kmd_local_reserve.py`, `test_agx_local_reserve_launch.py`.

## 4. Compatibility plan with the standing KMD

- **Wire ABI stays 6.** Both extensions are additive commands behind QUERY_CAPS, exactly like SYSMEM and LIVE_ADDITIVE.
- **Old KMD on the new broker:**
  - QUERY_CAPS is never sent, or the cap bit is ignored. Local grants stay LEGACY, with today's behaviour bit for bit.
  - 2b applies, which by construction refuses only pages no production path maps.
  - Hardware checkpoint (below): no OWNERSHIP refusal in the KMD receipts for a normal GM run.
- **New KMD on the old broker:** QUERY_CAPS answers INVALID, so the cap is absent. The KMD keeps the legacy local
  path; no new command is sent.
- **New KMD on the new broker:** negotiates at broker init (after ATTACH_MAILBOX, before the first CREATE) and then uses
  REGISTER_LOCAL_BACKING for every local page.
  - KMD side (GPU line): `TryLeafBackingEx` passes kind Local with the allocation identity and shared-ness.
  - Shared-ness comes from VidMm: an allocation created shared, or opened in another process through
    DxgkDdiOpenAllocation, is shared; everything else is exclusive.
  - VidMm moves pages: the KMD already revokes before re-granting (revoke refuses while referenced). A missed revoke
    becomes OWNERSHIP ("unavailable"), not a leak.
- **Build gating.** m1n1 flag `AGX_GPUVA_LOCAL_EXCLUSIVE` for 2a. 2b is unconditional once landed; it is the part that
  protects every KMD.
  - The default build must stay bit-identical until the flag is set. Hence 2b also sits behind its own flag
    `AGX_GPUVA_DENY_BACKEND` for the first standing comparison, and becomes default only after its hardware
    checkpoint passes (auto-promotion rule).

## 5. Performance cost on the grant path

- 2a: one byte read plus one compare per local grant, three stores on success. The revoke adds a counter decrement.
  `table_add` gets faster: O(1) instead of the 128-owner loop.
- 2b: two range compares on the IPA and two on the PA per grant and table registration.
- Both are negligible against `translate_page`, which already performs eight `hv_ipa_to_pa` walks per page
  (`hv_agx_retained_platform.c:70-73`).
- No change on the leaf-update path, the job path or the TLB path.
- Measurement: the `HV_EL2_TIME` instr-v5 spans (HV_V5_SP_CHECK / CMD slots, `hv_agx_gpuva_v5.h:178-205`) record grant
  time before and after. Expected within noise.

## 6. Standalone-boot profile

- The broker, the reserve selection and the dispatch are the same m1n1 code in assisted and standalone launches:
  - `local_reserve_select` uses `J313_AUTONOMOUS_LAYOUT` and the contract snapshot (`hv_agx_power_mmio.c:500-560`);
  - the broker is created by the KMD's own ACTIVATE (`hv_agx_retained_platform.c:366-400`).
- No host tool takes part. So both changes apply identically to the standalone image, with no launcher or Mu change.
  This is I: the code paths were read, but no standalone run was checked.
- Memory: +640 KiB of static broker state (2a), within the existing broker BSS of several MiB (the built-in backing
  table alone is about 6 MiB).

## 7. Hardware checkpoint (proposal, later)

- One standing GM run with m1n1 = standing + `AGX_GPUVA_DENY_BACKEND=1`. This is 2b alone with the old KMD, one
  variable.
- Pass: no OWNERSHIP or INVALID from the broker in the KMD receipts (G3 LastStatus counters); GM score within noise;
  instr-v5 grant times within noise.
- Recovery: standing m1n1 9bddd9aa.
- 2a needs the GPU line's KMD change first, so its checkpoint is a separate package run.

## 8. Open items

- Field names are proposed to the GPU line: CAP_LOCAL_EXCLUSIVE = 8, NEGOTIATE_LOCAL, REGISTER_LOCAL_BACKING,
  LOCAL_SHARED Flags bit 0, LOCAL_ABI = 1. Command numbers are appended after NEGOTIATE_LIVE (20 and 21). The broker
  header is shared, so they must agree before either side codes.
- U: the KMD's stable cross-process allocation identity for a VidMm local allocation. This is the GPU line's choice; any
  nonzero 64-bit value other than local_base works for the broker.
- U: whether any current KMD path registers a table in the scan-out scratch. Analysis I says no. The 2b boundary test
  and the hardware checkpoint settle it.

PORTABILITY: all bounds come from the reserve receipt and the profile constants, so no SoC constant appears in logic.
KEEP/DROP: decide after the §7 checkpoint.
