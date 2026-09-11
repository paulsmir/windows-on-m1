# J313 AGX G2 Firmware Regions and TTBR Codec Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Publish the accepted J313 firmware-memory regions in the immutable Windows contract and encode the exact m1n1-compatible TTBR0/TTBR1 pair offline.

**Architecture:** The G2 generator derives `gpu`, `shared`, `handoff` and `rtkit_private` only from the hash-bound G1 inventory and emits Windows constants without adding ACPI resources. The freestanding UAT codec validates both roots before writing a 16-byte pair with `VALID`, `BADDR` and per-context `ASID`; a separate clear operation invalidates both entries.

**Tech Stack:** Python contract generator, freestanding C11, sanitizer-backed tests.

**Spec:** `documentation/design/2026-08-27-j313-agx-g2-windows-uat-initdata.md`

## Global Constraints

- No handwritten J313 region address enters driver code.
- The new regions are contract metadata, not new ACPI `_CRS` descriptors.
- TTBR encoding follows pinned m1n1 fields: `ASID[63:48]`, `BADDR[47:1]`, `VALID[0]`.
- Both roots validate before output changes; clearing always invalidates both entries.
- No WDK callback or hardware path calls the codec in this increment.

---

### Task 1: Generated firmware-region contract

- [x] Add failing tests for exact accepted region tuples and generated macros.
- [x] Derive the four regions from the hash-bound G1 contract.
- [x] Regenerate checked-in outputs and prove ACPI output is unchanged.

### Task 2: Pure TTBR pair codec

- [x] Add failing literal tests for context zero, context 63, invalid roots and clearing.
- [x] Implement validate-then-write pair encoding and explicit pair clearing.
- [x] Run UAT sanitizer tests.

### Task 3: Verify and record

- [x] Run the complete public suite.
- [x] Commit code, add a CSV record with its exact hash, commit documentation and push.
