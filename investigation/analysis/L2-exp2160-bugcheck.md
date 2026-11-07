# EXP2160 bugcheck 0x113/0x10: offline root cause (L2 step 1)

Scope: 32d363ef (L2 step 1) + 4a740709 (eviction retry), both reverted
(6bcae831, 70e740da). Inputs: minidump `!analyze` in
`.local/handoff/codex/exp2160-crash/cdb-output-1.txt`; sources at 4a740709;
Microsoft Learn "Fast Mutexes and Guarded Mutexes".

## Verdict

The DDI returned to dxgkrnl at **APC_LEVEL**, not with a bad status.
A status cannot be the cause. On Windows 8 and later `KGUARDED_MUTEX` is a fast
mutex. Acquiring it raises IRQL to APC_LEVEL and saves the previous IRQL in the
mutex, and releasing it restores that saved IRQL. L2 step 1 takes P
(`process->Lock`) with a *try* while G (`state->Lock`) is already held, then
releases G before P. That order is not LIFO, so the final release restores
APC_LEVEL.

Microsoft Learn: "Starting with Windows 8, guarded mutexes and fast mutexes
are implemented identically". Fast mutex acquire "raise[s] the current IRQL
to APC_LEVEL, and ExReleaseFastMutex restores the original IRQL".

## Evidence ranking

1. **IRQL leak through non-LIFO release (confirmed offline, matches the dump).**
   - Bugcheck args: P3=0, P4=1. That reads as entry IRQL PASSIVE_LEVEL (0) and
     exit IRQL APC_LEVEL (1). The check is raised in dxgkrnl's own thunk
     (`dxgkrnl!ADAPTER_RENDER::DdiBuildPagingBuffer+0x270` ->
     `watchdog!WdLogSingleEntry5`) after our DDI returned. It is not a VidMm
     status check. Invalid statuses have always surfaced as 0x10E/0xB from
     dxgmms2 (EXP1014/1025/1320).
   - Exact sequence for VidMm's paging process, first UPDATE_PAGE_TABLE
     (`gpuva_g3_paging_windows.c` @32d363ef):
     1. L2060 `ExAcquireFastMutex(G)` at PASSIVE: G.OldIrql=0, IRQL=1.
     2. L2061 `AdmissionG3FindLockedProcess` -> `AdmissionG3LockFoundProcess`
        (`gpuva_g3_windows.c:329`) `KeTryToAcquireGuardedMutex(P)` succeeds
        (P is uncontended at boot) at IRQL 1: P.OldIrql=1.
     3. The loop breaks (no job in flight) with P and G held. The update
        succeeds.
     4. L2245 `ExReleaseFastMutex(G)` -> IRQL=0 while P is still held.
     5. L2246 `KeReleaseGuardedMutex(P)` -> IRQL=**1** (P.OldIrql).
     6. The function returns STATUS_SUCCESS at APC_LEVEL, and
        `...Checked` (L1612) passes it through. The dxgkrnl thunk sees
        IRQL 0 -> 1 and raises 0x113/0x10.
   - Why 32d363ef is new: before it, G was the only lock, and one fast mutex
     always restores PASSIVE. After it, only the uncontended
     (try-succeeds-under-G) path breaks. That is the common case, so the first
     call breaks. The contended "dropped" path is LIFO and correct: it releases
     G, acquires P at 0 (Old=0), acquires G at 1 (Old=1), then releases G (1)
     and P (0). The P->G sites (`KeAcquireGuardedMutex(P)` then G, release G
     then P) are LIFO and correct.
   - The first boot-time caller of the try-under-G pattern is
     `InitPagingProcessVaSpace`'s UPDATE_PAGE_TABLE. No job, escape, or
     completion has run by then. That matches the stack exactly.
   - Nothing here is specific to CpuOnlyMappings. Any process found through
     `AdmissionG3FindLockedProcess` with a free P fails the same way.

2. **A returned failure status: excluded.** `AdmissionGpuvaG3BuildPagingBufferChecked`
   (L1612-1645) rewrites every disallowed UPDATE_PAGE_TABLE/FLUSH_TLB status
   to STATUS_SUCCESS. If P3 were a status, it would be STATUS_SUCCESS.

3. **NULL from FindLocked / rundown: excluded.** `ExAcquireRundownProtection`
   is reached only when the try fails. A NULL result is handled as before
   (INVALID_DEVICE_STATE, which Checked masks).

4. **Recursive P (self-deadlock) and P leaked on an early return: excluded.**
   I enumerated every UPDATE/FLUSH/Encode exit: each one releases P exactly
   once. A recursive acquire or a leaked P would hang the thread or leave an
   unreleased fast mutex (also IRQL 1). In that case the next DDI on the same
   P would hang, not bugcheck at the first return. Sites that try P under G
   and release P before G are LIFO and correct (`PrivateReset` L1075,
   `StoreManagerSnapshot` L4205, `EvictPressure` L919). The 4a740709 retry is
   also not involved: no private scenes exist at boot.

5. **Entry IRQL > APC_LEVEL: excluded.** `AdmissionGpuvaG3BuildPagingBuffer`
   refuses non-PASSIVE entry (L1825), and VidMm calls it from its PASSIVE
   worker thread.

## All defective sites in 32d363ef (try P under G, release G then P)

Each returns or continues at APC_LEVEL whenever the try succeeds:

- `gpuva_g3_paging_windows.c`:
  - Checked: L1632/1636.
  - FLUSH_TLB: L1886, with the in-loop release at L1903/1904 and the final
    release at L1982/1983.
  - UPDATE_PAGE_TABLE: L2061, with the in-loop release at L2092/2093 and the
    final release at L2245/2246.
- `gpuva_g3_windows.c`:
  - `AdmissionG3LockFoundProcess` itself (L326-338), plus every caller:
    - FrameArm (L1451)
    - NativeQuery (L1677)
    - Copy escape (L1970 and its retry releases)
    - PrivateEscape (L2694)
    - CompleteJobTimestamp and AbortJob on the non-dropped path
  - `CaptureFaultMapping` (L51 try, L144-145 release).
  - The in-loop releases also lower IRQL to 0 while P is held, then restore it
    to 1. The waits then run at APC_LEVEL, and every later iteration nests
    further.

## Why the tests missed it

`tests/g3_vidmm_replay_shim.h` (4a740709, L122-133) encodes the wrong model:
"A guarded mutex saves no IRQL, so its release order is free." Its
`KeAcquireGuardedMutex` and `KeTryToAcquireGuardedMutex` leave `replay_irql`
unchanged. Its `ExAcquireFastMutex` asserts entry at PASSIVE, which forbids
the legal P->G order under the real semantics.

## Minimal offline test that would have caught it

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A DDI that returns at an IRQL
other than its entry IRQL because two mutexes were released out of order.

Shim change (`g3_vidmm_replay_shim.h`, also g4_submit_virtual_replay): model
both mutex kinds as Windows 8+ fast mutexes.

```c
typedef struct { int Held; KIRQL OldIrql; } KGUARDED_MUTEX; /* FAST_MUTEX alike */
static void replay_mutex_acquire(int *held, KIRQL *old) {
  assert(replay_irql <= 1); assert(!*held); *held = 1; *old = replay_irql; replay_irql = 1; }
static void replay_mutex_release(int *held, KIRQL old) { assert(*held); *held = 0; replay_irql = old; }
/* KeTryToAcquireGuardedMutex: if (Held) return FALSE; else replay_mutex_acquire */
```

Give `FAST_MUTEX` the same `{Held, OldIrql}` shape, and keep the rank checker.
Scenario (`g3_vidmm_replay_scenarios.c`):
1. CreateProcess with `Flags.SystemProcess=1`.
2. Issue one level-2 GPU_PHYSICAL UPDATE_PAGE_TABLE
   (InitialUpdate) through `AdmissionGpuvaG3BuildPagingBufferChecked`.
3. `assert(replay_irql == PASSIVE_LEVEL)` after it returns.
4. Repeat for FLUSH_TLB and for one in-flight-job wait iteration (R155 hook).

At 32d363ef this is RED: IRQL is 1 after the return. The generic guard is to
assert `replay_irql == PASSIVE_LEVEL` after every DDI entry point the replays
call.

## Fix direction (for L2 retry, not implemented here)

Keep the Windows 8+ rule: release mutexes in reverse acquisition order.
Options:
- Never take P under G: find the process under G with a rundown reference,
  release G, `KeAcquireGuardedMutex(P)`, then retake G. This is always
  P-then-G and always LIFO. It costs one extra G round trip per lookup.
- After a successful try-under-G, release P before G. That only works where
  the caller can drop P first, so it is usually wrong for the paging paths.

Both satisfy the new shim.
