# Phase 3 normalized compiler integration

Date: 2026-09-12.  Compiler-only; no hardware, package, source-tree, or
math-constant change was made on the Windows builder.

## Inputs and ownership

The runner uses clang-cl 20.1.8 at the supplied builder path, the pinned Mesa
tree `C:\Users\pauls\AD04-d3d10-frontend-build\mesa`, generated b5 headers, and
the existing fourcc, uatomic, agx_index, off_t, and LUT derived overlays.  It
copies the Mesa compiler directory only into a fresh GUID-named temporary
directory, then replaces the two hash-derived files there.  It refuses an
existing requested result path.  Its manifest records every command, arguments,
stdout/stderr path, and compiler/process exit status.

## Focused executable checks

- `uatomic-x64`: compile 0, run 0.  The Add64 wrapper now adds `uint64_t` bit
  representations and `memcpy`s the modulo-2^64 result to `__int64`; both
  `INT64_MAX + 1` and `INT64_MIN - 1` execute with the required bit-preserving
  updated value.
- `lut-derived-x64`: compile 0, run 0.  It checks 13 required LUT2 expressions,
  every LUT2 inversion (16 × 2), and every LUT3 inversion (256 × 3) against an
  independent truth-bit toggle oracle.  Native pinned output and Windows
  derived output are byte-identical: `01 02 04 06 07 08 09 0b 0d 0e 0a 05 06`.
- `agxindex-actual-x64`: compile 0, run 0.  The fragment script verifies the
  pinned header SHA-256, extracts upstream enum/record/constructor text, and
  records fragment hashes.  Native pinned and Windows derived records are
  byte-identical across 11 representative initialized constructor/modifier
  states.  The audited one-bit assignments are Boolean expressions, literals,
  or copies of another one-bit field; no concrete non-0/1 assignment was found.

The off_t mapping remains accepted and unchanged.  Its storage derives from
`util_dynarray.size`, which is `unsigned` and asserted as four bytes.  The
upstream `(int32_t)target - (int32_t)offset` branch-patch subtraction remains
an inherited range limitation: the present proof does not establish that every
unsigned emission offset is representable as `int32_t`, and this run does not
alter the pack algorithm.

## Exact compiler result

Both exact `agx_compile.c` controls (x64 and ARM64) exit 1 only after all
accepted overlays.  Their first errors are `M_LOG2E`, `M_PI`, and `M_1_PI`
undeclared in NIR/AGX code.  No lower-case atomic intrinsic, 20-byte agx_index,
off_t, UTIL_LUT2, or BSD fourcc/ioccom admission error appears.  These math
constants are intentionally not fixed here.

Raw captures, hashes, overlay diffs, field audit, and manifest are adjacent.
