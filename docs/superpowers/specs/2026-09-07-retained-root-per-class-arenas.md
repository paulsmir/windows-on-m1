# Retained-root per-class Windows VA arenas

## Goal

Preserve the native AGX allocator-class identity required by the EXP208 command
graph while retaining the EXP474 retained-root ownership model.  The first
hardware discriminator changes only the context-0 mappings for objects whose
native addresses belong to the A040 shared and A071 timestamp bands.  Existing
A000 production mappings and A020 command objects remain unchanged.

## Source evidence

- `drivers/apple-agx/shared/src/apple_agx_render_template.generated.c` records
  context-0 objects 20--31 in `0xffffffa040...` and objects 32--35 in
  `0xffffffa071...`; objects 0--17 are A000 and objects 18--19 are A020.
- EXP570--EXP574 prove physical TA reaches `FinalizeTA`, but retirement does not
  publish the shared stamp, event, or done pointer.  Flushing the complete graph
  and restoring the low 14 address bits did not change that boundary.
- Current production assigns every context-0 render object from one sequential
  A000 range.  This erases the native allocator-class portion of the GPU VA.
- The retained-root implementation owns TTBR1 root identity, preserves firmware
  prefix slots 0/1, and permits Windows mappings only below its own slot-2 page
  tables.  Windows never receives raw page-table access.

## Ownership and lifetime

`PREPARE -> firmware boot -> ACTIVATE -> QUERY_ARENA -> Windows relayout -> MAP
-> management/application/runtime -> UNMAP -> CLOSE`.

- Firmware owns retained root identity, prefix slots 0/1, firmware pages and
  native mappings.  Windows cannot read or mutate their tables.
- The broker owns range discovery, arena publication, validation, table
  mutation, barriers/TLB maintenance, rollback and exact mapping handles.
- Windows owns only the physical pages it allocated and the mappings created
  for those pages through authenticated broker calls in the current epoch.
- Cleanup unmaps only exact Windows handles.  It never frees the retained root,
  firmware descendants, or native objects.

## Versioned bounded contract

Retained-root ABI version 4 adds read-only `AGX_RR_QUERY_ARENA`.  A request
identifies one class; the response returns class, arena version, base and bytes.
Unknown classes, wrong state/epoch, nonzero unused request fields, malformed
alignment, or occupied ranges fail closed.

The first implementation publishes two non-overlapping broker-selected windows
inside the native allocator bands, deliberately away from the exact EXP208
object VAs:

- shared: `0xffffffa041000000`, 16 MiB, for original A040 objects 20--31;
- timestamp: `0xffffffa071100000`, 16 MiB, for original A071 objects 32--35.

The existing A000 production range and low buffer-manager alias retain their
current contract.  A020 is not broadened in this experiment.  A class arena is
usable only after a successful current-epoch query.  `MAP`, `QUERY`, `UNMAP`,
and `VERIFY_ABSENT` accept the new windows only under the same page alignment,
guest-RAM translation, PA exclusion, handle ownership, prefix integrity,
rollback, sync, and TLB rules as the existing range.

## Windows integration

After `ACTIVATE` and before the first production graph `MAP`, Windows queries
both class arenas.  It validates ABI version, class, range, size, alignment,
band, and non-overlap.  It then reassigns only render-shared objects 20--35:

- objects 20--31 are laid out sequentially in the shared arena;
- objects 32--35 are laid out sequentially in the timestamp arena;
- every object retains its captured low-14-bit intra-page offset;
- A000/A020 objects and all other initdata/channel/RegionB mappings remain
  byte-for-byte and address-for-address unchanged.

The relayout updates the existing mapping inventory by object identity, then
rebinds the existing production relocation objects and reapplies the existing
159-relocation table.  No alternate command builder or second memory backend is
introduced.

## Deterministic proof

Host tests must prove: ABI-v4 validation; read-only arena query; unknown class,
bad epoch, bad alignment, bad length and occupied range rejection; no map before
query; map/query/exact-unmap only inside the queried class range; private/system
range denial; prefix/root identity stability; rollback without leaked tables;
byte-exact preservation of A000/A020 addresses and low-14 offsets; class-correct
A040/A071 addresses; mapping-inventory rebinding; relocation/queue pointer
consistency; repeated lifetime does not reuse stale reservations.

## Hardware discriminator

EXP575 changes one causal variable: A040/A071 render-shared objects use the
queried broker-owned native class arenas.  PASS requires the existing
Windows-originated packet to pass the EXP574 `RetireStamp` boundary and produce
at least one new authenticated shared-stamp/event/done/completion receipt.  If
the retirement scalars remain byte-identical, the class-identity hypothesis is
rejected and EXP575 is not repeated.

## A020 command-arena addendum

EXP575/576 hardware rejected A040/A071 placement and moved the boundary before
physical TA. Exact standalone comparison instead identifies objects 18/19 as
native `cmdbuf` allocations in A020, while EXP574 executed through FinalizeTA
with those WorkCommand objects rebased into A000 but did not retire the queue.

ABI v4 therefore adds class `COMMAND` at the broker-selected free window
`0xffffffa021000000/0x01000000`. It has the same current-epoch query-before-map,
guest-page ownership, bounds, exact-handle, rollback, barrier, TLB and cleanup
rules. Production maps only objects18/19 there with effective address
`OriginalGpuVa + 0x01000000`; all other render-shared objects use the EXP574
all-A000 layout. Exact original A020 mappings remain denied.
