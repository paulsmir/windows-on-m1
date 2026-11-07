# Offline portable storage helpers

This is reusable host-tested C, not a StorPort miniport or a hardware-qualified
storage path. All fixtures are synthetic. No machine access is required.

## Inspected sources and ownership

- Repository `documentation/NATIVE_ANS_STORAGE.md` and phases 15–16 plus the
  offline matrix in `documentation/plans/J313_NATIVE_ANS_REQUEST.md` define the
  4 KiB LBA, 128 KiB transfer and 33-data-page limits. Constants come from the
  generated `../include/apple_ans_broker_abi.h`, not another handwritten ABI.
- [Microsoft SRB interface](https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/storport-s-srb-interface-with-the-storage-class-driver)
  and [16-byte extended CDB](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/ns-storport-_srbex_data_scsi_cdb16)
  establish the eventual input boundary. The wrapper must extract exact CDB and
  transfer lengths, validate path/target/LUN and direction, preserve request
  lifetime, serialize namespace/sense snapshots, and report real SRB status.
- [Microsoft StorPortGetScatterGatherList](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nf-storport-storportgetscattergatherlist)
  and [SG element](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/ns-storport-_stor_scatter_gather_element)
  define immutable per-request DMA spans, valid only until SRB completion.
- [Seagate SCSI reference revision J](https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf),
  clauses 2.1–2.4; 3.6, 3.10–3.11, 3.16, 3.18, 3.21–3.22, 3.33, 3.37,
  3.49, 3.51–3.53, 3.60, 3.62; mode headers and caching page in clause 5.3:
  consulted for CDB layouts and response formats. Zero-count READ/WRITE(10/16)
  transfers nothing. Capacity reports the last LBA. Unsupported fields produce
  INVALID FIELD IN CDB; unsupported opcodes produce INVALID COMMAND OPERATION
  CODE. Write protection is DATA PROTECT/WRITE PROTECTED. Mode DPOFUA describes
  DPO and FUA together; both remain unsupported here.
- [NVMe 1.4c](https://nvmexpress.org/wp-content/uploads/NVM-Express-1_4c-2021.06.28-Ratified.pdf),
  PRP and completion status definitions; [NVM command set 1.0c](https://nvmexpress.org/wp-content/uploads/NVM-Express-NVM-Command-Set-Specification-1.0c-2022.10.03-Ratified.pdf),
  Flush/Read/Write: command opcodes, zero-based NLB and phase-independent status
  decoding. A valid SCSI synchronization range maps to a full-namespace Flush.
  It always returns SUBMIT and must wait for actual successful completion.

No external implementation code was copied. Linux is not a source for the
Windows-facing CDB ABI. These helpers own only deterministic parsing, response
metadata and logical address representation. EL2 must independently authorize
DMA memory, direction, generation, namespace bounds and read-only policy; EL2
owns physical initialization, interrupts, DMA translation, power and recovery.

## Deliberately narrow behavior

`ans_scsi_prepare` returns COMPLETE (optional bounded metadata), CHECK (sense),
or SUBMIT (logical NVMe fields). It never touches request payload or hardware.
READ/WRITE require exact byte counts. Byte1 flags, nonzero group/control,
protection, DPO and FUA are rejected. No UNMAP/TRIM, MODE SELECT, saved/default
mode pages, power transitions or fake START STOP success. MODE SENSE(6/10)
supports cache page 08/all-pages with current or changeable parameters and a
short block descriptor unless DBD is set. Cache fields require `cache_known`
from a trustworthy backend; no changeable cache settings are exposed. INQUIRY
identifiers are caller-supplied fixed-width fields; only VPD page 00 is supported.

REQUEST SENSE creates fixed or descriptor-format data from the wrapper's supplied snapshot,
or NO SENSE. The wrapper must consume contingent sense correctly (including
successful autosense) and implement unit-attention/reset state separately.
Known NVMe errors map to sense; every unknown failure stays a failure. The
wrapper must translate CHECK to CHECK CONDITION plus the appropriate SRB error
and autosense flags. This portable layer deliberately does not invent WDK ABI
values or claim classpnp enumeration compatibility.

`ans_sg_to_prps` returns an ordered logical page vector, not a hardware PRP list.
PRP1 may have a DWORD-aligned offset; later pages must be 4 KiB aligned.
Contiguous SG pieces within one page coalesce; an interior discontinuity cannot
be represented and is rejected. Duplicate pages, wrapped addresses, zero spans,
wrong totals and excessive sizes fail with zeroed output. Caller must supply
non-overlapping input/output objects. For one page use PRP1, for two use direct
PRP2, and for three or more materialize entries 1..count-1 in registered,
page-aligned guest metadata and point PRP2 there. EL2 must snapshot and translate
that metadata into protected shadow backing. CPU granule is not the PRP granule.
No payload memcpy, allocation, bounce buffer or mapping is performed.

## Deterministic verification and remaining boundary

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Invalid CDB lengths/flags,
incorrect zero-count/NLB/endianness, multiplication and namespace overflow,
read-only bypass, metadata overrun, fake flush completion, failed status reported
as success, and DMA misrepresentation across fragmented spans.

Run `tests/run.sh`: C11, `-Wall -Wextra -Werror -pedantic`, ASan and UBSan.
Tests exercise all four read/write opcodes, every byte1/group/control bit,
capacity saturation/truncation, namespace end and UINT64_MAX, ready/block-size
gates, cache and sense metadata, failure statuses, and all 1,024 legal PRP1
DWORD offsets at maximum transfer with fragmented pages.

No regression claims apply to hardware. The smallest later checkpoint remains
ANS001 (unbound ANS0 and absent virtual NVMe with USB/RAM WinPE), only after all
offline gates and explicit user permission; recovery is the separately recorded
hash-verified G2 pair. These common helpers alone do not satisfy WDK build,
miniport lifetime, pending-sense synchronization, queue ownership, authorization,
completion/reset cleanup, signing or hardware persistence gates.
