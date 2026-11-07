# C15: checked C++ result boundary for the owned DXIL adapter

## WINDOWS CONTRACT:

This unlinked CPU adapter keeps the existing public owned-conversion ABI and
admission profile: compute 6.0, one entry point, static singleton CBV/raw-SRV/
raw-UAV resources. It adds no runtime callback, shader-model response or Windows
error mapping. Readable input, runtime-private capacity and publication into a
real DDI remain separate contracts.

## AGX/ASAHI CONTRACT:

The result remains owned SPIR-V plus an immutable binding map and workgroup size.
Legacy DXBC dispatch and the downstream NIR-map pass are unchanged. No AGX
backend, preprocessing, GPU allocation, KMD, m1n1 or Mu behavior is added.

## TRANSLATION:

All nineteen selected converter operations use original project
`agx_checked_*` C++ wrappers with typed results and noexcept declarations.
The adapter contains no direct upstream function invocation. Version/scope,
parse, stage/entry queries, scan, converter creation, setters, conversion,
SPIR-V/workgroup access and cleanup cross that same boundary. Ordinary
unsupported values remain distinct from reported query failures.

The adapter copies SPIR-V while the converter is alive and publishes only after
checked converter/blob/scope cleanup succeeds. Sticky scan refusal, stable
class/space/register sorting, dense per-kind slots and output ownership remain
the prior implementation's rules. A reported cleanup failure discards the job;
no promise of safe next-job reuse follows from a failed cleanup.

The facade is original MIT project code. Its source and the proposed adapter
are byte-identical to the already reviewed composite1421 candidate. No upstream
implementation body is imported into the repository. The external dependency
remains the selected `8434b0639fb2a48ce13aac08a420ecce5d23df0e` base plus reviewed
composite patch
`df3470cac9939a1acf0f0f11982970971d409476fe1a3141ccd8b7cae17bb863`.
The 197-input closure, nine matching archives, original notices and N01–N14
conditions remain required. Pristine archives alone are insufficient; there is
no direct-API or pristine fallback. Production build/package hooks remain
unlinked pending a matching native closure and coherent runtime contract.

The selected C++ translation units and facade require a uniform reviewed
exception policy and compatible CRT. Host tests use the accepted exceptions
configuration; this checkpoint establishes no new Windows link result.

## WHAT IS STILL UNKNOWN:

Catch-and-report behavior is not general internal allocation-failure recovery.
It cannot recover termination or a cleanup/destructor contract violation.
Foreign TLS scope/nonoverlap and input stability requirements remain in force.
The test performs no allocation-fault injection or fault sweep. Windows runtime
lifetime/error policy, native shipping linkage, full resource admission and SM6
capabilities remain open.

## Checkpoint and recovery

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Bypassing the checked boundary,
mixing pristine/composite dependencies, losing resource-map distinctions,
publishing borrowed output or accepting new unexpected diagnostics.

EXP1444 recompiles five project/fixture units, links twice against the matching
nine archives, and runs the existing one-key/mixed fixtures. Both fixtures and
their assertions remain unchanged. All nine commands return zero; actual MD
dependencies and pre/post pins are checked. The new mixed SPIR-V and map are
byte-identical to accepted1421. That ties the producer to the exact input of the
previous actual-NIR semantic proof; no new NIR execution is claimed.

Two controller stops remain preserved. The first successful compile exposed an
unlisted SDKSettings JSON dependency; it was explicitly pinned before a fresh
run with unchanged source/flags. A later controller stop assumed empty nominal
stderr, although the existing fixtures intentionally print the following exact
accepted diagnostics:

| Fixture | Bytes | SHA-256 |
|---|---:|---|
| one-key | 71 | `ab62d6b58d1f09325e097d5ff3b8a842f3905f062b347fe21b9584b642bec3f3` |
| mixed | 34 | `367e9e9d4c58cc409a0e747e339e1b571c85d5257b215e0dbba9cfcf083b8a51` |

Both hashes were verified against the accepted1421 artifact manifest. A separately
recorded continuation ran only the remaining two commands, preserving the
original STOP and raw captures. All other stderr remains required empty; any
different nominal byte stops the gate. The supplemental result is
`709e3c53f467a9035b7731145ff656e186bd6acbca4cec51b7f032609cf2e5e5`.

The CURRENT ROOT opt-in reproduces all nine commands in 9.701 s with 7008 unchanged
pins, actual dependency closure, the exact diagnostics and accepted SPIR-V/map.
Default discovery skips before importing the helper or accessing external files.
Historical pristine recipes and receipts remain intact; the new approved recipe
is the current checked/composite path. No hardware experiment is justified by
this deterministic CPU adoption; the normal inert guest remains unchanged.

## Full-suite comparison

The suite ran 1612 tests in 221.525 s: 117 existing failures/errors (67 failures,
50 errors), no added failures, 16 skips, and all seven source pins unchanged.
The initial comparison wrapper stopped because it required identical failure
sets. Its original result and STOP remain intact. The two removed outcomes are
unchanged serial-guard tests: the prior run recorded an already-owned global
`.local/air.lock` and a BlockingIOError; the current run passes those tests.
Both test and guard source bytes match the previous commit. This is recorded as
a shared-lock environment difference, with no C15 or serial-fix credit.
Supplementary review: `.local/c15-checked-current-full1444/root-comparison-review.json`.
Full log SHA-256:
`fdc681e1e4afe07fc6aabef6e17da59f8a8aca11e01196b97001b322d72a9b85`.
The suite remains failing; the claim is absence of added failures.
