# Phase 3 integration audit

Architect checkout remains `f90402c7589236ec9e8a030f4b825b5a36280866`.
Integration worktree: `.worktrees/integration-ad04-windows-compiler`, branch
`integration/ad04-windows-compiler`. No push or architect branch movement.

## Verified ancestry

One linear chain contains all accepted work; no repeated cherry-picks:

```
f90402c architect/orchestration
  ea0e0d8 uatomic implementation
  4a0b6f1 evidence
  21d51e5 gcc_struct probe
  d7be0d8 ms_struct pragma probe
  d49e6ad homogeneous index implementation
  a39fc61 evidence
  3416c77 internal offset implementation
  ec2b22d evidence
  8416715 LUT guard implementation
  9eb0cc4 evidence
```

Integration starts by fast-forwarding this exact chain, preserving historical
probe evidence. This records provenance; it does not upgrade historical test
claims. The branch is provisional until normalization checks pass.

## Concrete audit findings and decisions

1. Add64 wrapper does signed C addition after atomic ExchangeAdd64. Boundary
   overflow is undefined; fix returned bit pattern with unsigned arithmetic and
   bit-preserving conversion. Verify max/min wrapping and unchanged old/new-value
   semantics. No change to intrinsic ordering or operation.
2. The index corpus manually re-declared a similar type and omitted actual
   upstream constructors. Preserve 256/256 as evidence for that surrogate;
   strengthen evidence by extracting the exact hash-pinned production definition
   and constructors. No index representation change authorized by this audit.
3. LUT feature tests copied macros/helpers. Preserve their feature proof; test
   actual pinned/derived util/lut.h with independent per-bit inversion oracle.
4. off_t replacement uses the same unsigned domain as emission->size. The
   int32_t subtraction is inherited, including its boundary limitations. Existing
   compile-only probe is not a runtime range proof. Do not silently claim full
   packer range validation or broaden this task into a packer redesign.
5. Recursive deletion of caller-supplied build Root is disallowed. Replace with
   refusal on preexisting outputs and fresh directories; verify hashes before
   compile, store each attempt's own manifest and logs.

No contrary hardware evidence exists. EXP680–682 are unchanged. Latest saved
exact compiler controls reach M_LOG2E/M_PI/M_1_PI. No math patch is authorized
until this normalization and the real local patch lane demonstration are ready.

## Scope enforcement limits

Old verify_worker_scope inspects tracked diff plus nonignored untracked files;
it does not enforce an OS sandbox, does not verify remote source hashes, and did
not observe changes performed outside the named worker directory. Earlier empty
worktree checks cannot alone prove that manually run builder commands preserved
source. Phase3 requires exact applied patch paths, clean HEAD/branch preflight,
immutable approved command definitions and captured input/output hashes.

Two lower-cost code-capable agents perform bounded audit and normalization; a
third builds the patch validator. Devstral itself only returns data through
Ollama; no inference response is shell execution. Successful patch proposal and
separate local review are still required to declare the local lane demonstrated.
