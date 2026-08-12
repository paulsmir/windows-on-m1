# Canonical J313 Build Verification — 2026-08-12

This report records the host-side verification of the canonical public build.
It does not claim hardware validation; assisted and standalone boot gates remain
open until they are exercised on the target J313.

## Source identity

- Root repository: `1c18e5942e40fb843f7d81a3166982ec1a749f92`
- m1n1: `9a95e65acb09508166591e69bc57fc2edd438036`
- Mu: `9dccb0133f244f2e4de7e3862dcb9f0ef7ba4776`
- Compiler recorded by both manifests: `Homebrew clang version 22.1.8`

All three repositories were on the named branch
`codex/canonical-public-release`. Their tracked files were clean. Untracked
local build and investigation material was deliberately retained until the
hardware gates pass.

## Release profile

- Path: `dist/j313/release`
- Display: `physical`
- Diagnostics: `off`
- `boot.bin` SHA-256:
  `8a85dd18e758313fffc5ca1ab50d2c5b1b7e8594c12b052759863e125d8d09c3`
- Mu firmware SHA-256:
  `a59503244e553566d0ad8ce4c00bb3d245e7ed63b3d82bb8580b5caa8f3e33fa`

The build completed from source, all generated hashes verified, and
`tools/check_release_binary.py` confirmed that the m1n1 image contains no
periodic FIQ, timer, SGI, or watchdog diagnostic paths. Fatal-only diagnostics
remain available for recovery.

## Debug profile

- Path: `dist/j313/debug`
- Display: `physical`
- Diagnostics: `monitor`
- `boot.bin` SHA-256:
  `a0eba894fcbdafd152f55d4f01f42536efc7cfa35dcb9ea9801b585258604c1f`
- Mu firmware SHA-256:
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`

The debug raw stage contains the expected bounded FIQ, SGI, watchdog,
bugcheck, and standalone USB-monitor diagnostics.

## Shared guest contract

Both manifests independently verified the same generated contract:

- 8 guest CPUs
- guest physical base `0x850000000`
- guest RAM end `0xa00000000`
- framebuffer base `0x85f000000`
- framebuffer `2560x1600`, stride `10240`
- guest-layout SHA-256:
  `c1d3206b6f04ae3e11038d22010ad760b031c55bed8f121574ca19e0cad5b358`

## Tests

- Root Python suite: 155 tests, all passed using `proxyenv/bin/python`.
- m1n1 host suite: all tests passed during both release and debug builds.
- Release and debug manifests: verified.
- Release and debug `SHA256SUMS`: verified.
- Standalone image outer and nested launch flags: verified (`0x1` release,
  `0x11` debug).

Running the root suite with the system Python is not supported: it lacks the
project's `pyserial` dependency. The documented `proxyenv` is the canonical
test and launch environment.

## Pending hardware gates

Before deleting the legacy checkout or tagging a release, the following must
pass on the MacBook Air J313:

1. Assisted debug boot to the Windows desktop with USB monitoring.
2. Assisted release boot to the Windows desktop without periodic diagnostics.
3. A bounded CPU and storage workload without the former long stalls.
4. Standalone installation followed by a cold boot to the Windows desktop.
