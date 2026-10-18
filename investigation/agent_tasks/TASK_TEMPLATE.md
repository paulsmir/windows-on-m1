# TASK_ID: <unique-id>

TIER: <A|B|C>
MODEL: <exact worker/model>
INPUT_COMMIT: <40-char commit>
WORKTREE: <absolute path>
BRANCH: <agent branch>

## GOAL

<One bounded result, not a project milestone.>

## WHY_THIS_TASK_EXISTS

<Concrete evidence that makes this task useful now.>

## CURRENT_PROVEN_STATE

<Only facts relevant to this task.>

## FIRST_UNKNOWN

<Exact question this task may answer; “what next?” is excluded unless explicit.>

## ALLOWED_PATHS

<Exact files or prefixes.>

## READ_ONLY_PATHS

<Exact files/trees worker may inspect.>

## FORBIDDEN_PATHS

<Explicit subsystems/worktrees that must not be modified.>

## ALLOWED_ACTIONS

<Exact commands/search/build/test actions.>

## FORBIDDEN_ACTIONS

<No scope expansion, architecture, capability, hardware, recovery, etc.>

SOURCE_CHANGE_ALLOWED: <YES|NO>
HARDWARE_ALLOWED: <YES|NO>
NETWORK_ALLOWED: <YES|NO>

## EXPECTED_COMMANDS_OR_TESTS

<Exact expected invocations, including target/toolchain.>

## SUCCESS_CRITERIA

<Observable completion conditions.>

## STOP_CONDITIONS

<When to capture evidence and return without trying variants.>

## REQUIRED_EVIDENCE

<Raw paths plus compact fields.>

## RETURN_FORMAT

Use `RETURN_TEMPLATE.md`; no architecture recommendation unless requested.
