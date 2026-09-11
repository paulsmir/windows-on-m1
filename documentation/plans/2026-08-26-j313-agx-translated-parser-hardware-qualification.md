# J313 AGX Translated Parser Hardware Qualification Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Prove in one fail-closed G2 boot that the corrected translated-resource parser advances AppleAgx beyond stage 3 and completes only the existing bounded power-broker receipt.

**Architecture:** Keep the EXP-124 G2 firmware, AGX SSDT, synthetic broker,
NVMe-safe m1n1 and EXP-123 recovery byte-for-byte unchanged. Stage only the
CI-signed qualification driver from run `33012247554`. Its sole functional
change is translated-resource parsing from commit
`a680ef2c451140c17c831d0d06df9ae82f3fb712`. The driver must still return
`STATUS_NOT_SUPPORTED`; GPU firmware, RTKit, SGX access, interrupt connection,
UAT, queues, render and display ownership remain forbidden.

**Tech Stack:** ARM64 WDDM miniport, WDK CI test signing, PowerShell PnP
collection, Mu ACPI, m1n1 broker and hypervisor, Windows 11 ARM64.

## Fixed identities

- Experiment: `EXP-20260826-127`; exactly one G2 boot and no retry.
- Root branch/head: `feature/j313-gpu-acceleration` /
  `61bd7998cd0e715a41e961bdd897bdcc9408cb80`.
- WDK run: `33012247554`; default and power-qualification ARM64 jobs passed.
- Driver manifest SHA-256:
  `192a253084f56d557f28c650ee8bbe18b37ca885d12a3b5e7b299662377c0b9b`.
- Driver SYS SHA-256:
  `81b08b27f1cdd9362937cd254b357792d321978c93a6bcc33b76d9e12788e124`.
- Catalog SHA-256:
  `d43ac3685b9556ca81aa66acfa0cb2391b072b5b742361fac9af3a60f5b8de25`.
- Signer: `2DAADA2A7B34687AE6D922D792F39C220EA4C7AA`.
- G2 manifest SHA-256:
  `02204a6e37a04a323eae05e24b6a35eb7a0c6327b9af98b39d714482d78a0c70`.
- Recovery manifest SHA-256:
  `143fd9aa07f9b224c316c5e23e3993991d7308fa178164beadc785e8dade03f9`.
- Candidate and recovery m1n1 SHA-256:
  `2c39f7723475e6e74fa00b1a88e413ed7e5159a0da1bac5286b6c0442b7d52a9`.
- Evidence path:
  `investigation/artifacts/EXP-20260826-127-agx-translated-parser/`.

## Procedure

- [x] Revalidate all package and firmware manifests; require evidence path absent.
- [x] Require recovery with eight CPUs, no AppleAgx/APPL0002/package/signer,
      Running AppleInput/NVMe/xHCI, zero critical events and zero Event 129.
- [x] Stage only the exact package and record its new `oemNN.inf` identity.
- [x] Shut down normally and launch one exact G2 candidate with display both,
      monitor logging and `WOM1_AGX_G2_POWER_BROKER=1`.
- [x] Require APPL0002 Problem 43, final StartDevice stage 7 with
      `STATUS_NOT_SUPPORTED`, and exact ordered ON/QUERY/OFF broker receipts.
- [x] Require zero GPU firmware, RTKit, SGX MMIO, interrupt connection, UAT,
      queue, render, present or display-ownership action.
- [x] Require eight CPUs, responsive input/storage/xHCI and zero critical
      events. Two candidate Event 129 resets rejected the storage gate.
- [x] Shut down normally, boot exact recovery, remove only the recorded package
      and exact signer without `/force`, then complete a cleanup reboot.
- [x] Require final clean recovery, hash evidence, record verdict and push.

## Result

The sole candidate boot advanced through translated-resource and state
validation to stage 7, returned `STATUS_NOT_SUPPORTED` (`0xC00000BB`) and
produced the exact successful broker sequence ON, QUERY, OFF. The forbidden
action audit was empty. Eight CPUs and the input, storage and xHCI services
remained available with zero critical events. Two `stornvme` Event 129 resets
reject the candidate storage gate, so the run does not authorize GPU firmware, RTKit,
SGX MMIO, interrupt connection, UAT, queues or rendering.

Recovery removed only `oem17.inf` and signer
`2DAADA2A7B34687AE6D922D792F39C220EA4C7AA` without `/force`. The required
cleanup reboot used the exact EXP-123 pair and ended with eight CPUs, no
present APPL0002, AppleAgx service, package or loaded module, Running
AppleInput/NVMe/xHCI, zero critical events and zero Event 129. EXP-127 is
closed and must not be retried.

## Falsifiable result

The hypothesis passes only if the final lifecycle breadcrumb is stage 7 with
`STATUS_NOT_SUPPORTED` and the broker log proves one bounded ON/QUERY/OFF
transaction while all forbidden GPU actions remain absent. Stage 3 failure
falsifies the parser correction. Stage 4-6 failure localizes the next boundary
but does not authorize a retry. Any storage reset, critical event, reset,
input loss, identity mismatch or incomplete rollback rejects the experiment.
