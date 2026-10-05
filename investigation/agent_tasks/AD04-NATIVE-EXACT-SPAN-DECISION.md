# ARCHITECT_DECISION — native exact source spans

Accepted after primary-source review of the pinned Mesa USC builder, cmdbuf.xml,
agx_compile.c and the current Windows capture/materializer contract.

For Draw command version exactly native USC v3, a read-only source reference for
Constant, ShaderRodata or UscPipeline may have an exact byte count that is not
a multiple of four. Its allocation-relative Offset remains four-byte aligned.
All other roles, writes/executes and v1/v2 retain the existing byte-count rule.
No wire layout, new length field, range rounding, source envelope or allocation
list ownership change is allowed.

Pinned Mesa evidence: pipeline allocation begins 64-byte aligned while its
builder advances a byte pointer by source-defined USC records: SHADER6,
NO_PRESHADER2 and the other 4/8-byte records. Hence 38/62 byte streams are
valid content extents. Compiler rodata begins at `agx_pad_binary(binary, 4)`
but is stored as exact `size_16 * 2`; final native uniform chunk can be two
bytes. Push Constant ranges are also exact `length * 2`; length is half-count
and may be odd. This is source-copy metadata, not a relaxation of pointer
encoding alignment or destination placement.

The current materializer already copies/hashes `Reference.Bytes` exactly and
aligns only its private storage placement to 16 bytes. It verifies relocation
field width against the exact destination object. Pointer kind alignment remains
separate: buffer address4, table address8, VDM-relative64. A two-byte source
extent never permits a two-byte pointer address.

Required deterministic tests: v3 exact USC38/62, rodata2 and Constant2/6;
same values reject v1/v2/other roles/non-Read and unaligned offsets; exact read
and hash with sentinel bytes outside the range; field-crosses-end, target-at-end,
encoding alignment/overflow, stale owner/range and source lifetime cases. Keep
v3 legacy overlay rejection. No hardware run follows this admission change.

Open separate work remains complete native graph/source-byte lifetime, placement
and full runtime dispatch/retirement. The pointer target check presently proves
point-in-range rather than every downstream hardware access footprint; do not
solve that uncertainty by broadening source ranges.
