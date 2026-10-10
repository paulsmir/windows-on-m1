# Pitfalls that already cost runs

Each entry: the symptom, why it happens, what to do.

## Lab ownership and state

- **Two sessions on one lab.** A second session's ordered restart or recovery
  reboots the guest under the first one's probe and both results are lost.
  Check running launchers/probes/recoveries and peer sessions before any
  guest-changing command (SKILL.md step 2).
- **Asking the operator to power on when it is not needed.** After a clean
  recovery Windows answers SSH and `run-probe.sh` reaches "Running proxy" by
  itself through the ordered restart. Ask for physical action only when
  Windows is unreachable and the proxy ports are absent.
- **A package carried into the next experiment.** Every experiment starts from
  the recovered GPU-visible profile with no AppleAgx package; always run
  `recover.sh`, including after failures and diagnostic runs.
- **Booting the normal profile with a package bound.** The normal profile
  exposes the raw AGX mailbox level lines (EXP1049). `recover.sh` cleans up in
  the GPU-hidden profile first and stops if cleanup is unverified; never work
  around that stop.

## Staging

- **Stage gate refuses: guest disk below the floor.** ETW captures and dumps
  accumulate. Compress old traces in place (`compact /c /s:<dir>`) or archive
  them to the host with their hashes, then delete only what is archived; never
  delete unarchived evidence.
- **Hash mismatch between host and guest.** `prepare.sh` stops before staging.
  Re-upload; if it persists, the guest copy is being modified (antivirus,
  another session) - find out before running.
- **Leftover old experiment numbers after cloning.** `clone_experiment.py`
  prints `leftovers:`; anything but `none` means a script still points at the
  old experiment.

## Guest automation

- **Loads do nothing when started over SSH.** An SSH session has no desktop.
  Start anything that draws or sends input with `arm_interactive` (a
  scheduled task in the interactive session).
- **Notepad opens more windows every run.** Windows 11 Notepad restores its
  previous session; `notepad-reset.ps1` moves its TabState/WindowState away
  (archived, not deleted) before the loads.
- **DWM idle during a synthetic drag.** Without user input the display can
  sleep and DWM stops composing (EXP1080: 68 presents in 117 s). The loads call
  `SetThreadExecutionState` and send one real mouse event first; keep that in
  new loads.
- **Garbled done files.** `Out-File` writes UTF-16LE; use `utf16_cat`.
- **Arguments starting with `-` through `arm-interactive.ps1 -Arguments`** are
  parsed as its own parameters; pass positional values only.
- **Quoting.** The guest's OpenSSH default shell is cmd.exe, so
  `powershell -Command "..."` sees `$_` literally; escape `$` for bash
  (`\$_`) and use `\"` for inner quotes. Copy the quoting of an existing
  helper rather than inventing new quoting.
- **Windows PowerShell treats native stderr as an error** under
  `$ErrorActionPreference='Stop'` (clang warnings abort builds). Judge native
  tools by `$LASTEXITCODE` and set `Continue` around them.

## Interpreting results

- **Probe order changes pacing.** A GL process, a post-load video self-test or
  a fresh Notepad before the loads moved typing/charmap pacing by 5-8 points
  (EXP1150/EXP1151 vs EXP1152). Keep `desktop-loads.sh` first and unchanged.
- **Same-size drag stutter is win32k**, not AppleAgx (EXP1145).
- **A "hang" that is a modal dialog.** A debug CRT assert in a GL client opens
  a MessageBox in the interactive session; the process looks hung. The smoke
  test writes minidumps and sends asserts to stderr; read the dump stack.
- **The application did not use the driver at all.** GoldSrc started in
  software mode after a stored video-mode failure (EXP1153) and later used the
  system GDI Generic OpenGL (EXP1155). Check the loaded modules and the
  renderer string before attributing results.
- **Profiling perturbs the load** (EXP1145 typing). Compare the load's rate
  under capture with an uncaptured run before trusting the profile.

## Bookkeeping

- **Estimated timestamps in the ledger.** Several ACTUAL lines needed
  corrections; take every time from `date -u` or from the evidence files.
- **Ledger rewritten instead of corrected.** Append a correction that names
  the original line.
- **Assistant attribution in commits.** `AGENTS.md` forbids `Co-Authored-By`
  and session links in this repository's commits, whatever a tool default says.
- **`git stash` in a shared repository.** Worktrees share the stash; use a WIP
  commit or a tagged stash applied by SHA, never bare `stash`/`pop`.
- **Credentials.** Never type the user's passwords or sign in to their
  accounts (Steam etc.); the user does that.
