# Hash-gated Phase 3 math validation

`RUN_MATH_CONTROL` accepts only the reviewed script SHA-256
`c1a5d554c941a78c3fa02e77c370e8cbb0e5ecb8291177f277f079a07cd67d13`.
It fixes the builder account/key/host policy, pinned normalized InputRoot,
fresh GUID result root, and exact probe/script paths; it accepts no caller
shell text. No hardware action occurred.

The source probe uses the CRT macro names directly and prints their in-memory
double representations via `memcpy`; it embeds no replacement literals.
Native output and Windows x64 output match:

- `M_LOG2E=3ff71547652b82fe`
- `M_PI=400921fb54442d18`
- `M_1_PI=3fd45f306dc9c883`

Windows x64 probe compile/run status is 0/0 and reports `_MSC_VER=1950`,
`__clang__=1`, `_WIN32=1`, and `aarch64=0`. ARM64 compile status is 0;
the ARM64 object is retained only as the compiler receipt and was not run.

The approved `/D_USE_MATH_DEFINES` script recorded compile exit 0 for both
exact `agx_compile.c` x64 and ARM64 controls. There is therefore no newly
exposed compiler blocker. The remote command returns 1 solely because the
reviewed script's unchanged final assertion still expects exactly two failed
compile entries; this validation does not modify that policy.

`runner-manifest.json` preserves the fixed command ID, verified script hash,
remote/result roots, and probe/full-control SSH statuses. `compile/manifest.json`
contains the exact commands, `/D_USE_MATH_DEFINES`, stdout/stderr paths, and
per-entry exits.
