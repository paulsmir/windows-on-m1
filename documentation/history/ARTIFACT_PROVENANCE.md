# J313 artifact provenance

This note preserves the useful identity of historical bring-up artifacts without publishing
the binaries, crash dumps, machine-local logs, or copied firmware trees that produced them.
The chronological engineering record is in [Development history](../DEVELOPMENT_HISTORY.md).

## Why historical binaries are not releases

Early assisted and standalone experiments were assembled from several private worktrees. Some
combined a newer proxy client with an older target, mixed 1280x800 and 2560x1600 framebuffer
contracts, or used a diagnostic m1n1 tree for a nominally quiet image. Those files remain useful
as evidence, but their filenames do not prove source identity and they must not be offered as
installable releases.

The canonical builder now publishes only `dist/j313/release/` and `dist/j313/debug/`. Every
profile contains a versioned `MANIFEST.json` with the root, m1n1, and Mu commits, compiler
identity, J313 guest contract, and hashes and sizes of every installable component. Launch and
ESP installation reject missing, mismatched, or legacy top-level artifacts.

## Retained control identities

The following immutable controls were important while separating compiler, launch-context, and
diagnostic effects:

| Purpose | m1n1/source identity | `boot.bin` SHA-256 | Result |
| --- | --- | --- | --- |
| Clang standalone CPU1 control | `c6dc965a4d3312e4aa437835236e6b0e98c32c16` | `f6df78592dc4c6b395c99e9cc6a6cd961e59001282355532adad74fcc9239ef1` | Reproduced the pre-guest CPU1 failure; GCC was not the cause. |
| Stage-0 self-chainload checkpoint | m1n1 `0e532a95f3e848f220836937beb9160fcafef386`, root `0de5c9b074f505aee2d506d2588ee2ff63332321` | `bb718eea9caf1edcc245c6358ba5421430834cf2c44fba81c9bb21684116eccd` | Validated the two-stage transition, then reproduced the same CPU1 boundary. |
| Watchdog snapshot experiment | local source label `cc2a46b-watchdog-snapshots` | `7cb721c11f09a6413e9915ca2a6f912e70199f04945c48ef30a257c6fd7cf9fb` | Diagnostic-only; not a release candidate. |
| FIQ fast-path experiment | local source label `cc2a46b+fiq-fastpath` | `e122ba443be049cd2babfcb21bef4825cf56d6a42cbd202ee6bbd87e86eff4fe` | Diagnostic candidate superseded by source-controlled policy changes. |

These values are for regression archaeology only. They do not satisfy the current manifest
schema and must not be installed by the public tooling.

## Current promotion rule

A current profile becomes publishable only when it is built from a clean recursive checkout,
passes host verification, and passes the assisted and standalone J313 hardware gates documented
in [Platform stability](../PLATFORM_STABILITY.md). The successful hardware run records the exact
generated manifest rather than introducing another descriptive filename.

