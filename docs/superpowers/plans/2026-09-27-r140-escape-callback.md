# R140 escape callback implementation plan

> Execute inline with superpowers:executing-plans; one final independent review.

**Goal:** Correct R137's runtime escape identity contract proven by the EXP855C Explorer dump.
**Spec:** `/Users/pavel/public_windows/.local/tandem/NEXT_TASK_R140.md`.
**Architecture:** Keep private scene ownership in KMD and AGX4 v3 unchanged. UMD passes the adapter runtime handle to EscapeCb and the owning runtime device plus context in D3DDDICB_ESCAPE. No Air writes, package, firmware or caps changes.
**Tech stack:** pinned WDK26100, real Windows UMD wrapper, x64 contract executable and ARM64 compile; host G3/G4 profiles16/64.

## Sources and ownership

Inspected EXP855C Application1000/UMD logs, exact Air d3d11.dll with Microsoft
PDB, saved Explorer8428 dump copied read-only, package855C UMD with matching PDB,
854B..855C UMD diff, umd_gpuva_windows.c, umd_runtime_device.c, umd_internal.h,
agx_win32_gpuva_batch.c, agx_d3d10_windows.cpp, pinned d3dumddi.h and Microsoft
PFND3DDDI_ESCAPECB/D3DDDICB_ESCAPE documentation.
The dump proves user-mode ABI misuse before KMD escape entry. Runtime owns
handle translation; UMD owns callback arguments; KMD owns private storage,
DMA/interrupt/completion/power through the existing broker. No ADT/register,
Asahi hardware, m1n1 or Mu behavior is at this boundary; no live hardware
measurement or change is needed. EXP855C Code0 and ordinary EXP377/392 Code28
contracts remain unchanged. Asahi has no Windows runtime handle contract.

## Work and verification

- [x] Add behavioral tests in umd_gpuva_contract_windows.c using the real
  AdmissionUmdGpuvaOperations()->PrivateEscape and a strict runtime callback.
  Assert adapter identity, device/context ownership, payload/output round trip,
  failure result, acquire/release, and no dispatch for missing owners/callbacks
  or a closing device. The pre-fix code must fail the identity test.
- [x] Fix only private_escape in umd_gpuva_windows.c: validate adapter identity
  and handles, set request.hDevice, pass RuntimeAdapter.handle.
- [x] Build/run GpuvaContractMode on x64 in the existing builder tree; compile
  ARM64. Run affected G3/G4 profiles and full host suite once. Compare exact
  failure/error names to the R139 baseline, not only totals.
- [x] Document dump attribution and hashes in R140 analysis; update compact
  next boundary; independent review; reread tandem review; explicit-path
  implementation commit, then CHANGES.csv row with commit and EXP855C.

Review focus: no fabricated runtime handles; callback NULL; stale/missing
adapter; nonzero context requires owning device; HRESULT failure must not
become success. No whitelist based on observed pointer or payload values.
Smallest later hardware checkpoint (not authorized here): same private acquire
passes EscapeCB into KMD and returns; then attribute any later failure from
fresh dump/receipts. Preserve known recovery and collect evidence first;
EXP855C's correction requires hidden Code45 cleanup for Code0, then ordinary
EXP377/392. This plan does not authorize that run.

Result: implementation and tests complete; full suite preserves exact R139
baseline failures, no new names. Independent review has no findings.
Implementation commit then ledger bookkeeping use explicit paths.
