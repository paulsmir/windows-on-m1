# ARCHITECT_DECISION — native USC relocation fields

Scope: FULL GRAPHICS, offline integration only. Retain physical/patch-list
architecture and current hardware-proven firmware/root/display contracts.

Primary input: pinned Mesa 9aa1215f878b504f66159dd2ead4c7973142126e,
src/asahi/genxml/cmdbuf.xml USC definitions, generated agx_pack.h, native
agx_build_pipeline in src/gallium/drivers/asahi/agx_state.c. Our transport,
ABI validator, relocation capture and dynamic materializer were inspected.
No new Windows DDI, MMIO, IRQ, memory model or capability is introduced.

## Contract

Version 3 retains version 2 layout and separate vertex/fragment USC references.
It adds only two kinds, rejected in versions 1 and 2:

- UscPreshaderOffset32: complete 8-byte USC record; Code bits32..63 relative
  to the KMD shader base. Preserve low32. Reject subtraction underflow/overflow.
  Source USC reference and target Shader reference retain existing ownership.
- UscTableAddress39: 8-byte texture/sampler USC record; Buffer bits27..62,
  shr3, target Descriptor reference. Preserve low27 and bit63. Alignment8,
  strict address below 2^39. No raw texture-pixel address substitution.

No packed stream record alignment is invented. Materializer uses byte access;
native unpack tests copy records to aligned scratch for the decoder's uint32
loads. Source objects are unchanged, materialized objects remain per-submit.
Capture source holds and Windows submission lifetime are unchanged.

## Executed verification

Real regression: reusing Shader would overwrite Preshader control; reusing
Uniform would overwrite table count bit26 and apply the wrong address domain.
RED: executable failed on capturing missing kind8. GREEN: capture -> v3 wire
validation -> KMD materializer -> native pack/unpack exact bytes for preshader,
texture and sampler, all 128 count encodings, two placements. Rejected old
versions, wrong width/target role, address misalignment, 39-bit overflow and
preshader relative underflow/overflow. Non-address bits and original source
bytes preserved. No native hardware command is executed by this fixture.

Source archive016 SHA256:
33eea0438605456e4968638d6aa95053bd00662e5dd6199636ae4bad3b283852.
Windows x64 executable SHA256:
e6d9d9ec5adff9a5f8d7681861e7094d832a9ea4cd95b17f542ccc9fffa862e9.
Windows x64 execution PASS. ARM64 build/link PASS; execution NOT_RUN.
Both use pinned LLVM20, WDK26100/MSVC14.44 environment with assertions enabled.
Host ASan/UBSan and eight related suites PASS. Raw Windows logs/commands/results:
investigation/evidence/AD04-native-usc-v3/.

## Remaining wiring, not acceptance

Native agx_build_pipeline typed capture is still required, including nested
descriptor/resource edges and their lifetimes. Legacy render_dynamic_overlay
deliberately rejects non-v1. Its placement/bindings and dynamic DMA job kind
validation require a separate coherent native integration slice; they were not
opened by this change. Mixed batch-pool roles/source allocation classes also
remain explicit. No candidate, signing, installation or hardware verdict.

Smallest future hardware checkpoint remains one fully tracked native draw through
the Windows composer and physical materializer, only after complete graph and
package gates; existing exact GPU recovery policy is unchanged.
