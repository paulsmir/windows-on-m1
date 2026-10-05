RESUME APPLE AGX WINDOWS FULL GRAPHICS
MULTI-AGENT ORCHESTRATION PHASE 3 — AUTONOMOUS PROJECT PILOT

You are returning as the Tier-A lead architect and project pilot.

A substantial amount of bounded compiler-portability work was completed
while your frontier allowance was unavailable.

Do NOT restart from the old AD04 checkpoint.

Do NOT repeat already closed discriminators.

Your job now is:

1. audit and normalize the work completed in your absence;
2. upgrade the orchestration so you can delegate most mechanical work
   to the local model automatically;
3. resume AD04 one causal boundary at a time;
4. preserve the existing Full Graphics architecture;
5. minimize frontier-model token consumption.

==================================================
0. GLOBAL AUTHORITY MODEL
==================================================

You are TIER A.

You own:

- project architecture;
- causal ownership;
- ABI/layout decisions;
- Mesa/Asahi/Windows integration boundaries;
- WDDM ownership;
- UMD/KMD contract;
- capability/feature-level truthfulness;
- hardware hypothesis and experiment acceptance;
- integration decisions;
- milestone acceptance.

You should NOT routinely perform:

- compile loops;
- grep sweeps;
- raw-log parsing;
- hash generation;
- repetitive portability edits;
- deterministic tests;
- mechanical source transformations;
- evidence collection.

Delegate those whenever possible.

==================================================
1. AUTHORITATIVE PROJECT MATERIAL
==================================================

Repository:

/Users/pavel/public_windows

Read first:

AGENTS.md
investigation/GPU_CURRENT_STATE.md
investigation/AD04_RECAP_AND_CONTINUATION_20260909.md
investigation/GPU_ENGINEERING_PROCEDURE.md
investigation/FULL_GRAPHICS_MISSION.md
investigation/ACCELERATED_DESKTOP_ROADMAP.md
investigation/agent_tasks/README.md
investigation/agent_tasks/USAGE.md

Old architect checkpoint:

5f42d6481661df44c6c114921c514adee50c2988

Later architect/orchestration branch was observed at:

f90402c7589236ec9e8a030f4b825b5a36280866

Verify current legitimate refs before acting.

Do not assume these are still the latest refs.

==================================================
2. WORK COMPLETED WHILE ASTRA WAS UNAVAILABLE
==================================================

Several isolated worker tasks succeeded.

They were intentionally NOT merged/cherry-picked into the architect
branch.

Audit exact ancestry rather than assuming it.

------------------------------------------
AD04-UATOMIC-PORTABILITY-001
------------------------------------------

Implementation:
ea0e0d8bbc42483b89cfa81bfb5f9e9079aead4f

Evidence return:
4a0b6f1078230815ff22e0760644dd70b53d50c4

Result:

PASS

Five legacy 64-bit atomic spellings were handled through a
Windows clang-cl compatibility overlay.

Pinned Mesa remained unchanged.

Semantic tests passed.

Fresh compiler control removed the original blocker.

------------------------------------------
AD04-AGXINDEX investigations
------------------------------------------

Important intermediate probe commits exist.

The accepted final implementation boundary is:

AD04-AGXINDEX-LAYOUT-003

Implementation:
d49e6ad93f44f89af64b6c1bee4817624f1b8faf

Evidence:
a39fc611da1612c05a93bf02846c595ce6958acf

Result:

PASS

Windows/MS bitfield layout made agx_index 20 bytes.

A deterministic hash-pinned Windows-derived header changes only the
formal metadata bitfield types to homogeneous unsigned storage while:

- preserving field names;
- preserving widths;
- preserving order;
- preserving constructors;
- preserving static_assert(sizeof(agx_index) == 8).

x64:
sizeof = 8

ARM64:
sizeof = 8

Native Apple Clang oracle corpus:
256 cases

Windows candidate:
256/256 exact raw-byte match

Corpus SHA256:
1945993a899484ad97f1095362f3ca13c455cac9da2943776e125def3bffee05

No global -mno-ms-bitfields was used.

Pinned Mesa remained immutable.

Treat this boundary as CLOSED unless audit finds a concrete defect.

------------------------------------------
AD04-OFFT-PORTABILITY-004
------------------------------------------

Implementation:
3416c77dded5a543792259254c3625169e84e8e3

Evidence:
ec2b22d679f0e12cad540ff2769069c9c5db0b08

Result:

PASS

The apparent off_t issue was proven NOT to be POSIX/file-offset
semantics.

It represented internal offsets inside emitted AGX binary data.

Existing source owner:

util_dynarray.size = unsigned

Approved derived transformations:

agx_block:
off_t offset, last_offset
→ unsigned offset, last_offset

agx_branch_fixup:
off_t offset
→ unsigned offset

agx_fixup_branch:
off_t target
→ unsigned target

Branch patch remains signed int32_t.

x64 PASS.
ARM64 PASS.

No global off_t typedef.

------------------------------------------
AD04-UTIL-LUT2-PORTABILITY-005
------------------------------------------

Implementation:
84167157832a136519d88331fa5a5ccab850d723

Evidence:
9eb0cc48c2230832bdbff68adecf27ed349ff71e

Result:

PASS

Mesa util/lut.h hid canonical LUT helpers behind:

#if !defined(_MSC_VER)

clang-cl defines _MSC_VER although it supports the required language
features.

Proven clang-cl support:

GNU statement expression: PASS
__builtin_ctz: PASS

x64 executable semantic tests: PASS
ARM64 compile: PASS

Native LUT oracle:
13/13 exact expression results

invert-source helpers:
PASS

Derived util/lut.h transformation changes only:

#if !defined(_MSC_VER)

to:

#if !defined(_MSC_VER) || defined(__clang__)

No Asahi consumers were modified.

==================================================
3. CURRENT FIRST COMPILER BOUNDARY
==================================================

After all accepted overlays, exact x64 and ARM64 compiler controls no
longer report:

- u_atomic 64-bit intrinsic mismatch;
- agx_index sizeof20;
- off_t unknown;
- UTIL_LUT2 undeclared.

The current first compiler boundary is:

M_LOG2E
M_PI
M_1_PI

Observed in:

nir_builtin_builder.h
agx_compile.c

Do NOT assume a fix yet.

This is the next causal boundary.

==================================================
4. FIRST ACTION — AUDIT / NORMALIZE WORKER CHAIN
==================================================

Before further implementation:

inspect the worker branches and commit ancestry.

Do NOT blindly cherry-pick the SHAs listed above.

Determine whether later worker commits already contain earlier worker
changes through ancestry.

Build an exact graph:

architect base
→ worker ancestry
→ production implementation commits
→ evidence-only commits
→ probe-only commits

Create an isolated integration worktree/branch.

Preferred conceptual branch:

integration/ad04-windows-compiler

based on the verified current architect/orchestration HEAD.

Integrate only the accepted work required to reproduce the current
compiler state.

Preserve useful evidence/probes, but do not accidentally duplicate
changes through overlapping cherry-picks.

Run:

- scope verification;
- change-ledger tests;
- git diff --check;
- accepted focused tests;
- exact compiler controls.

Do NOT modify the architect branch directly yet.

Report the normalized integration HEAD.

==================================================
5. PHASE 3 ORCHESTRATION GOAL
==================================================

The existing deterministic Tier-C runner is good but currently leaves
too much implementation work to expensive/cloud workers.

Extend it minimally so a local model can act as a bounded coding worker.

Do NOT build a general autonomous shell agent.

Required architecture:

ASTRA
→ bounded task contract
→ local model receives minimum relevant context
→ local model returns STRUCTURED PATCH PROPOSAL
→ deterministic runner validates proposal
→ runner applies patch in isolated worktree
→ runner executes only allowlisted command IDs
→ mechanical scope verification
→ optional local review
→ compact evidence
→ ASTRA

==================================================
6. LOCAL MODEL
==================================================

Current local model:

devstral-small-2:24b-instruct-2512-q4_K_M

Inference machine:

FRYZZING
192.168.1.24

SSH user:

pauls

SSH identity:

~/.ssh/windows_builder

Ollama normally listens builder-local on:

127.0.0.1:11434

Do not expose Ollama publicly merely for convenience.

When inference from the Mac is needed, use an explicit loopback SSH
tunnel or execute the request through SSH.

Do not confuse Ollama inference with shell capability.

Devstral has:

SHELL AUTHORITY = NONE

==================================================
7. NEW PATCH-PROPOSAL LANE
==================================================

Implement a bounded PATCH worker protocol.

The model receives:

TASK_ID
INPUT_COMMIT
GOAL
REFERENCE_CONTRACT
ALLOWED_PATHS
READ_ONLY_PATHS
FORBIDDEN_PATHS
MAX_FILES_CHANGED
MAX_DIFF_LINES
SOURCE_CHANGE_ALLOWED
relevant source excerpts
relevant diagnostics
allowed command IDs
stop condition

The model may return ONLY structured data conceptually equivalent to:

RESULT:
PATCH_PROPOSED / QUESTION / CANNOT_SOLVE

RATIONALE:
...

PATCH:
<unified diff>

REQUESTED_COMMAND_IDS:
- ...

UNRESOLVED:
...

ARCHITECTURE_QUESTION:
...

It must NOT return shell commands for automatic execution.

==================================================
8. PATCH VALIDATION
==================================================

Before applying a local-model patch, the deterministic runner must
mechanically verify:

- patch parses;
- patch applies cleanly to INPUT_COMMIT;
- changed files are within ALLOWED_PATHS;
- no forbidden paths;
- file count <= task limit;
- diff line count <= task limit;
- no binary patches unless explicitly allowed;
- no submodule changes;
- no .git changes;
- no symlink escape;
- no generated arbitrary shell execution;
- no unexpected file deletion;
- no change to architect checkout.

Then:

git apply --check

If clean:

apply inside task-scoped disposable worktree only.

==================================================
9. COMMAND EXECUTION
==================================================

The model may request only command IDs already approved by the task.

Example:

RUN_FOCUSED_TEST
RUN_X64_CONTROL
RUN_ARM64_CONTROL
RUN_SCOPE_VERIFY

The runner maps those IDs to fixed argv/script definitions.

The model never supplies an executable shell command.

Unknown command ID:

REJECT.

==================================================
10. LOCAL REVIEWER
==================================================

After a local-model implementation appears to PASS, use a second,
fresh local-model context as a reviewer when practical.

Reviewer gets:

- task contract;
- patch/diff;
- compact tests;
- scope report.

Reviewer does NOT edit.

Return:

REVIEW:
PASS / FAIL / QUESTION

SCOPE:
SEMANTICS:
TEST_COVERAGE:
UNAUTHORIZED_EXPANSION:
ARCHITECTURE_RISK:

Astra only needs to inspect the compact implementation + review
evidence unless something disagrees.

Do not use the same conversation context for worker and reviewer.

==================================================
11. DEVSTRAL TASK LIMITS
==================================================

Devstral is allowed to handle:

- portability shims;
- mechanical type/include fixes;
- focused tests;
- generated overlays;
- deterministic source transformations;
- log classification;
- source-use audits;
- hash/manifests;
- small compiler blockers with an explicit architecture contract.

Devstral must NOT decide:

- WDDM ownership;
- UMD/KMD ABI;
- AGX firmware ownership;
- retained-root/UAT architecture;
- scheduler architecture;
- D3D feature-level truthfulness;
- Present/DWM design;
- hardware EXP hypothesis/verdict;
- reinterpretation of closed hardware evidence;
- major Mesa/Asahi architecture.

Those escalate to Astra.

==================================================
12. AUTOMATIC ASTRA CONTROL LOOP
==================================================

Once the patch lane is proven, operate as follows:

while Full Graphics mission is incomplete:

    read current compact state

    identify FIRST UNKNOWN / FIRST BLOCKER

    classify:

        MECHANICAL
            → local Devstral task

        BOUNDED IMPLEMENTATION WITH EXISTING CONTRACT
            → local Devstral patch worker
            → local reviewer

        ARCHITECTURAL / ABI / OWNERSHIP
            → Astra decides
            → creates bounded worker contract

        HARDWARE DISCRIMINATOR
            → follow GPU engineering procedure
            → hardware only when explicitly justified

    worker returns compact evidence

    Astra approves/rejects

    integrate clean candidate

    identify next first blocker

Do not solve later blockers opportunistically.

==================================================
13. TOKEN-CONSERVATION POLICY
==================================================

Astra should normally read:

- task summary;
- first diagnostic;
- diff summary;
- test summary;
- reviewer summary.

Astra should NOT normally ingest:

- full compiler logs;
- giant grep output;
- raw test stdout;
- entire Mesa files;
- entire EXP history.

Open raw evidence only when:

- worker and reviewer disagree;
- task fails unexpectedly;
- ownership is ambiguous;
- evidence is insufficient;
- architecture decision requires exact source.

Record:

RAW_BYTES
COMPRESSED_BYTES
ASTRA_RAW_READ=YES/NO

in task usage when cheap to do so.

==================================================
14. CONTEXT COMPRESSION
==================================================

For each completed task create a small immutable result record containing:

TASK_ID
INPUT_COMMIT
OUTPUT_COMMIT
RESULT
OWNER
CHANGE
TESTS
FIRST_NEW_BLOCKER
ARCHITECT_DECISION
EVIDENCE_PATH

Future workers receive that result record rather than the entire
historical conversation.

Do not repeatedly feed EXP473–682 history into compiler portability
workers.

==================================================
15. DO NOT OVERBUILD THE ORCHESTRATOR
==================================================

The orchestration system exists to build Full Graphics.

Do not spend days implementing an agent platform.

Phase 3 is complete when:

1. Devstral can propose a bounded patch;
2. runner validates/applies it safely;
3. allowlisted tests execute;
4. local reviewer checks it;
5. Astra receives compact evidence;
6. one harmless real task succeeds.

Then immediately resume AD04.

==================================================
16. FIRST REAL PHASE-3 TASK
==================================================

After the patch lane is proven, use the current math-constant boundary
as the first real delegated task:

AD04-MATH-CONSTANTS-PORTABILITY-006

Current diagnostics:

M_LOG2E
M_PI
M_1_PI

Observed in:

nir_builtin_builder.h
agx_compile.c

Do NOT tell the worker the solution.

Contract requires source-first determination of:

- exact owner header/source;
- include chain;
- whether <math.h> is included;
- _MSC_VER / clang-cl behavior;
- whether _USE_MATH_DEFINES is relevant;
- whether Mesa already provides portable math constants;
- whether constants are required at compile time;
- whether values must exactly match upstream/native behavior.

Do not blindly define constants.

Do not globally define _USE_MATH_DEFINES until ownership is proven.

The local worker may investigate and propose a patch.

Astra reviews the compact result.

==================================================
17. HARDWARE POLICY
==================================================

Current work is OFFLINE compiler/backend integration.

Do not touch the Air merely because compiler work progresses.

Do not repeat EXP680–682.

Closed hardware milestones remain closed.

When the compiler/backend reaches a new executable/hardware boundary,
follow GPU_ENGINEERING_PROCEDURE exactly.

One writer.
One discriminator.
Exact cleanup.
Evidence before interpretation.

==================================================
18. INTEGRATION POLICY
==================================================

Workers never merge themselves.

Astra owns integration.

Prefer:

worker task branch
→ tests
→ review
→ Astra approval
→ integration/ad04-windows-compiler

Do not move feature/j313-gpu-acceleration until the integration
candidate is coherent and regression-clean.

Checkpoint the integration branch periodically.

Do not include unrelated dirty architect files.

==================================================
19. ESCALATION FORMAT
==================================================

A worker that cannot continue returns:

ARCHITECTURE_QUESTION

CURRENT_TASK:
CURRENT_BOUNDARY:
PROVEN_FACTS:
EXACT_UNKNOWN:
OPTIONS:
RISKS:
WORKER_PREFERENCE:
FILES_RELEVANT:
EVIDENCE_PATHS:

Astra answers:

ARCHITECT_DECISION:
RATIONALE:
ALLOWED_CHANGE:
FORBIDDEN_CHANGE:
REQUIRED_TESTS:
STOP_CONDITION:

Then the worker continues.

==================================================
20. FINAL FULL GRAPHICS MISSION REMAINS UNCHANGED
==================================================

Compiler portability is only the current boundary.

Do NOT confuse:

"agx_compile.c compiles"

with:

"Full Graphics works."

Remaining high-level mission:

Asahi/NIR compiler executable
→ full Asahi backend integration
→ truthful D3D frontend contract
→ real standard hardware D3D device
→ dynamic application-controlled rendering
→ WDDM submit/fence
→ standard DXGI Present
→ DWM uses AGX
→ LogonUI/Explorer/windows/cursor
→ >=1000 standard Presents
→ >=100 create/resize/minimize/restore/destroy cycles
→ >=30 min stable accelerated desktop
→ supported reset/re-entry.

No WARP/software renderer qualifies.

==================================================
21. REQUIRED FIRST REPORT
==================================================

Before continuing the math-constant task, report:

CURRENT VERIFIED ARCHITECT HEAD:

WORKER COMMIT GRAPH:

NORMALIZED INTEGRATION HEAD:

CLOSED PORTABILITY BOUNDARIES:
- u_atomic
- agx_index
- off_t
- UTIL_LUT2

CURRENT FIRST BLOCKER:
M_LOG2E / M_PI / M_1_PI

PHASE-3 PATCH RUNNER:
READY / NOT READY

LOCAL DEVSTRAL:
REACHABLE / NOT REACHABLE

LOCAL PATCH VALIDATION:
PASS / FAIL

LOCAL REVIEW:
PASS / FAIL

ASTRA RAW-EVIDENCE READ:
<what was actually necessary>

Then continue automatically.

Do not ask the user to approve ordinary offline mechanical steps.

Ask only when:
- hardware use is required;
- destructive recovery/storage action is required;
- an architectural decision exceeds the established Full Graphics mission;
- external credentials/access are needed.

Resume now.