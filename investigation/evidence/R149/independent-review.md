# R149 independent review

Reviewer scope: current production diff and the two new host regressions,
independent of the QUERY audit. No production changes, full suite, builder,
package, hardware or commit were performed by this review.

**Decision: ACCEPT the two corrections as software-only changes. No blocking
correctness finding in the reviewed diff.** Full-suite baseline comparison,
ARM64 `/W4 /WX /analyze` validation and final commit/ledger steps belong to the
parent task and are not asserted by this review. Neither change proves the
cause of EXP862 SSH loss or identifies CPU0's process/module.

## Rejected-submit error propagation

Inspected production `AgxWin32AsahiBatchFinish`, `AgxWin32GpuvaSubmit`,
`AgxWin32AsahiBatchPoll/Abort/Release`, `AgxD3d10WindowsFlushStatus`, BO
`agx_bo_unreference/dispose`, `AgxWin32AsahiCollect/Detach`, and generated
native `agx_batch_submit`, cleanup and frontend `Flush` bodies.

- Existing native submission already makes rejection terminal for the context
  via `ctx->any_faults`. Setting `Backend.Failed` on the existing fail label
  publishes the same failure to Windows' status policy. It does not convert a
  successful native submission into an error.
- `FlushStatus` observes the backend flag and returns an error (existing
  `LastScreenError` when available, otherwise `E_FAIL`); actual generated
  frontend `Flush` calls `SetError` on that result.
- The correction intentionally leaves `Gpuva.Terminal` separate. Failed
  admission with successful eviction has no uncertain accepted command/fence.
  Its rejected batch polls complete and remains releasable. The release and
  BO destruction paths do not refuse cleanup merely for `Backend.Failed`.
  Conversely, existing uncertainty still sets `Gpuva.Terminal`, preserving
  its release veto and ownership holds.
- The real-body regression executes production BatchFinish, GpuvaSubmit,
  parser, FlushStatus and the generated frontend Flush body. Boundary callbacks
  simulate residency, submit rejection and Windows error delivery. It checks
  one frontend error and keeps rollback, private-lease and reference-release
  assertions. It also exercises preparation failures and successful cases.
- The recorded RED output reports `rejected=1 terminal=0 frontend_errors=0`;
  the independently rerun GREEN reports `frontend_errors=1 status=80004005`.

Scope limitation: BatchFinish's initial precondition returns and unrelated
native `ctx->any_faults` paths are not changed. The fix and its claim must stay
specific to failures reaching BatchFinish's existing fail label. This review
does not establish a missing timeout in an accepted-command fence wait, nor a
spin at the captured CPU0 PC.

## USC placement and encoding

Inspected the new shared constants, actual Attach and Bind implementations,
BO-create class/flags routing, parser `usc_va`, builder ISP/TA execution-base
writes, and pinned Mesa `agx_device.c/.h` USC heap/address helper. The native
generator's Attach call and existing callers supply `0x1100000000`.

- Pinned native USC addressing subtracts `shader_base` and asserts the offset
  fits 32 bits. Mesa's heap base is 4 GiB aligned. Thus a low-VA allocation is
  low relative to that window; it is not necessarily an absolute VA below
  4 GiB. The old zero-base UMD contract disagreed with the existing parser and
  firmware execution-base contract.
- Placement now uses `[base+64KiB, base+4GiB)` and Attach uses the same base.
  Reserving the first page avoids ambiguous zero USC offsets. Relative
  pipeline truncation and `agx_usc_addr` agree because the base is 4 GiB
  aligned. The parser expands those offsets to the actually mapped address.
- Builder and parser changes substitute the shared constant for the identical
  previous numeric execution base. They do not relax validation or change
  firmware layout. General-BO limits remain unchanged.
- Bounds checks reject returned addresses below minimum, misalignment, end of
  window and range overruns before Map. Subtraction after the upper-bound test
  avoids unsigned wrap. Invalid accepted reservations are freed; failed free
  preserves the preexisting terminal-ownership policy.
- BO-create still distinguishes non-EXEC LOW_VA pipeline slabs from EXEC
  shader BOs and derives protection independently. No particular captured
  hardware BO token/base is fabricated from `0x11002b0140`.
- Microsoft documents process-owned reservations, 64 KiB alignment, and
  `BaseAddress+Size <= MaximumAddress`; the new minimum/maximum contract
  follows those semantics. Source:
  [D3DDDI_RESERVEGPUVIRTUALADDRESS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-d3dddi_reservegpuvirtualaddress).

The synthetic exact-shaped and independent moved-address cases test the
current-source coordinate defect. Dynamic ordinal14 -> Bg.Usc remains the
pinned-producer inference stated in `R149-submit.md`, not a field tag from
hardware. The correction is justified by the coordinate contract independently
of that specific field attribution. The real hardware allocation identity,
successful GPU execution, and guest survival remain unproven.

## Independent validation

Both commands completed successfully using Clang, `-Wall -Wextra -Werror`,
ASan and UBSan:

```
python3 -m unittest discover -s tests -p test_r149_submit_error.py -v
python3 -m unittest discover -s tests -p test_g4_usc_window_replay.py -v
```

Each reports one passing unittest; those tests execute multiple success,
failure and boundary cases. This is targeted validation only. No hardware
verdict is inferred from these passes.

The final USC test revision was rereviewed and rerun after adding separate
non-EXEC pipeline and EXEC helper cases (including unchanged protection flags),
and a second compilation without `APPLE_AGX_GPUVA_WINSYS` proving arbitrary
construction-base behavior is unchanged and invokes no GPUVA callbacks. Both
profiles pass. The recorded USC RED output in
`investigation/evidence/R149-submit/usc-red.txt` has the expected modeled
ordinal14/`0x11002b0140` rejection with mapping `0x2b0000` and shader base zero.

Final reviewed production and regression file hashes are recorded in adjacent
`independent-review-hashes.json`. Later modifications require review of their
delta; these hashes do not include unrelated working-tree/submodule changes.
