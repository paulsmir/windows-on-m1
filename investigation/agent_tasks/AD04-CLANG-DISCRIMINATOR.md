# TASK_ID: AD04-CLANG-DISCRIMINATOR

TIER: C
MODEL: Devstral Small 2 24B through verified builder-local Ollama
INPUT_COMMIT: 5f42d6481661df44c6c114921c514adee50c2988
WORKTREE: /Users/pavel/public_windows/.worktrees/devstral-mechanical
BRANCH: agent/devstral-mechanical

## GOAL

Compile the same pinned Asahi compiler source/control fixture using the already
SHA-verified LLVM/Clang 20.1.8 on FRYZZING and report one exact first portability
blocker without semantic source changes.

## WHY_THIS_TASK_EXISTS

AD04 MSVC b2 removed the timespec and BSD-header first errors but reached the
native `sizeof(agx_index) == 8` assertion and missing `off_t`. LLVM20.1.8 is
prepared and version-verified, but its same-source result is unknown.

## CURRENT_PROVEN_STATE

Physical AGX lower layers and EXP680–682 are closed. This is an offline compiler
control only. Pinned Mesa frontend source is 9aa1215f878b504f66159dd2ead4c7973142126e.
LLVM20.1.8 archive SHA256 is
3197846a2b19063687dd56e93e34cd941e3548d907f23a6131571321bdf9fe7b.

## FIRST_UNKNOWN

Does clang-cl compile the unchanged `agx_compile.c` control past the MSVC internal
packing failure, and what is the first real remaining Windows portability blocker?

## ALLOWED_PATHS

- `investigation/evidence/AD04-asahi-windows-compiler/`

## READ_ONLY_PATHS

- `.local/reference/mesa/`
- `.local/experiments/AD04-asahi-windows-compiler/`
- `investigation/AD04_RECAP_AND_CONTINUATION_20260909.md`

## FORBIDDEN_PATHS

- all `drivers/`, `mu/`, `m1n1_windows/`, `config/`, hardware scripts and
  all files outside ALLOWED_PATHS
- all existing `.worktrees/*` except this task worktree

## ALLOWED_ACTIONS

- verify `clang-cl --version` and SHA/provenance already recorded;
- copy/run the exact isolated compiler control on FRYZZING;
- collect raw stdout/stderr, tool version and result JSON under ALLOWED_PATHS;
- run scope verifier and evidence collector.

## FORBIDDEN_ACTIONS

- no source change; no assertion removal; no bitfield/layout flags guessed;
- no WDK ABI changes; no package build/install; no hardware; no retry with
  alternative flags after the first unexpected result; no next-action decision.

SOURCE_CHANGE_ALLOWED: NO
HARDWARE_ALLOWED: NO
NETWORK_ALLOWED: YES, only verified SSH/Ollama/toolchain control plane

## EXPECTED_COMMANDS_OR_TESTS

Use builder-local `C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe`.
Compile exact pinned `src/asahi/compiler/agx_compile.c` under the existing b2
isolated include control. Do not treat completion of a source compile as a
complete compiler or UMD test. Run `verify_worker_scope.py` afterwards.

## SUCCESS_CRITERIA

One compact evidence bundle contains: CLANG TOOLCHAIN, TARGET, SOURCE_CHANGED,
AGX_INDEX ASSERTION, TIMESPEC, FOURCC/BSD HEADER, OFF_T, COMPILE RESULT,
FIXTURE RESULT, FIRST REAL PORTABILITY BLOCKER and raw-log path.

## STOP_CONDITIONS

Builder/tunnel unavailable; SHA/version mismatch; compiler returns nonzero; first
unexpected diagnostic; any needed source/ABI/architecture decision; scope violation.

## REQUIRED_EVIDENCE

Raw build log, exact command, version output, result JSON and scope JSON below
`investigation/evidence/AD04-asahi-windows-compiler/clang/`.

## RETURN_FORMAT

Use `RETURN_TEMPLATE.md`. No recommendation for how to fix the first error.
