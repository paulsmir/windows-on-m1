# J313 AGX G2 Windows UAT and Initdata Boundary

## Decision

The next AGX milestone adds a pure, version-pinned UAT encoder and initdata
envelope builder, followed by an inert Windows MMIO transport.  The code is
compiled and exhaustively tested offline before any hardware package can use
it.  The stable J313 Windows package remains unchanged unless a dedicated
qualification build flag is enabled.

This milestone does not boot AGX firmware, connect GPU interrupts, advertise a
render node, submit work or modify DCP scanout.  Its purpose is to establish
the exact memory and structure boundary required by the already accepted
direct-Windows-ownership architecture.

## Source and version boundary

All constants and structure fields are pinned to these sources:

- project m1n1 submodule commit `bee53dc60bd160c0a64de758974af767c2970baf`;
- Asahi Linux `asahi` branch commit
  `77cb8f24c2381a8abb7272d7bbdec548d6426a8a`;
- J313 contract generation `G13` / firmware structure version `V13_5`;
- Microsoft `DXGKDDI_START_DEVICE`, `DXGK_DEVICE_INFO` and
  `DXGKCB_MAP_MEMORY` contracts.

Source behavior may be reimplemented, but source code is not copied into the
Windows driver.  A future firmware generation or structure version requires a
new explicit codec implementation and new golden-layout tests.  It must not
fall through to the G13/V13_5 implementation.

## Considered approaches

### Structure-first, separately testable components — selected

Build the UAT encoder, allocation inventory and small top-level initdata
envelope as freestanding C modules.  The Windows transport supplies mapped
MMIO and allocated buffers through typed descriptors.  Each component has a
strict fail-closed interface and host tests before WDK compilation.

This is the smallest path that preserves direct ownership while keeping binary
layout errors away from hardware.

### Full m1n1/Asahi initdata port in one change — rejected

The complete initdata graph contains many generation-specific HWData and
channel structures.  Porting it as one unit would be difficult to review,
would obscure provenance and would make a single layout error indistinguishable
from a power, mailbox or UAT failure.

### EL2-built initdata passed to Windows — rejected

This would reduce initial KMD code but create two owners for firmware memory,
UAT teardown and reset.  It would also make PnP and TDR dependent on a private
runtime service across the virtualization boundary.

## Exact address geometry

The two address-width values describe different sides of translation and must
not be conflated:

- UAT input address size: 39 bits;
- UAT output physical address size: 40 bits;
- page size: `0x4000` bytes;
- page offset: 14 bits;
- translation levels: three, with shifts `36`, `25`, `14`;
- entries per level: `8`, `2048`, `2048`;
- contexts: 64.

Context zero is firmware-private.  Context 63 remains reserved for bounded
qualification work already described by the G2 contract.  Future process and
render address spaces may use only contexts 1 through 62 and are outside this
milestone.

The encoder accepts only 16-KiB-aligned virtual addresses, physical addresses
and lengths.  It rejects input addresses outside 39 bits, output addresses
outside 40 bits, arithmetic overflow, duplicate or overlapping mappings,
unknown memory attributes, invalid context ownership and write-plus-execute
permissions.

The initial implementation emits table and page descriptors but does not own
physical allocation.  Callers provide zeroed, aligned table pages through an
explicit callback and receive an inventory containing every allocated table
and mapping.  Teardown consumes that inventory in reverse order.  This makes
partial construction deterministic and independently testable.

## UAT permissions and cache policy

The public interface exposes semantic protections rather than raw PTE bits:

- firmware device MMIO read/write;
- firmware shared uncached read/write;
- firmware private cached read/write;
- firmware and GPU private cached read/write;
- firmware read/write plus GPU read-only;
- GPU shared uncached read, write or read/write.

The codec converts these values to the exact G13 PTE fields proved by the
pinned sources: owner, AP, UXN, PXN, attribute index, shareability, access flag,
non-global and descriptor type.  Callers cannot provide arbitrary raw flags.
Executable mappings are not exposed in this milestone.

## Versioned initdata envelope

The builder produces only the top-level G13/V13_5 initdata envelope and its
three UAT level descriptors.  It does not fabricate the complete runtime
pointer, globals or HWData graphs.

The input descriptor contains typed, already allocated GPU addresses for:

- the tagged `IDTA` buffer;
- runtime pointers;
- globals;
- firmware status.

The G13/V13_5 codec writes the proved version tuple
`{0x6ba0, 0x1f28, 0x601, 0xb0}`, page size `0x4000`, page bits `14`, three
levels, level shifts and counts from the address contract, and
`host_mapped_fw_allocations=1`.  Each level's output mask derives from the
40-bit physical width and 16-KiB page mask; it is not a handwritten literal.

The builder requires a zeroed destination buffer of the exact generated size,
checks every pointer against the firmware-private UAT policy and writes no
output on validation failure.  A successful result returns both the encoded
length and a hashable manifest of the version and referenced buffers.

Nested runtime structures will be introduced as separate versioned codecs in
later milestones.  The lifecycle core cannot publish this envelope until all
required descriptors have been supplied by those codecs.

## Windows MMIO transport

ACPI exposes one non-cacheable, exclusive SGX resource:

- base `0x204000000`;
- length `0x04000000`.

ASC is the verified subrange at base `0x206400000`, length `0x0006c000`.
It is not a second ACPI resource.  `AppleAgxDdiStartDevice` obtains translated
resources from `DxgkCbGetDeviceInformation`, validates the complete existing
resource contract, maps the SGX resource exactly once through
`DxgkCbMapMemory`, and derives the ASC virtual subview only after checked
subtraction and range containment.  The synthetic power broker remains a
separate mapping.

Mapping code is compiled only under a new qualification flag and is not called
by the default package.  The first qualified action is map, validate the two
virtual views without register access, unmap and return the existing
fail-closed `STATUS_NOT_SUPPORTED`.  No SGX or ASC register is read or written
by that experiment.

The adapter owns explicit mapping records containing physical base, size,
mapped virtual base and active state.  Stop, failed start, surprise removal and
reset all call one idempotent reverse-teardown routine.  A partially mapped
adapter cannot progress to firmware ownership.

## Component boundaries

The implementation is divided as follows:

- `apple_agx_uat`: pure geometry validation, descriptor encoding and mapping
  inventory; no WDK or allocation policy;
- `apple_agx_initdata`: pure G13/V13_5 envelope validation and encoding; no
  mailbox, MMIO or allocation;
- generated J313 contract: the only source for platform addresses, geometry,
  version and deadlines;
- Windows transport: dxgkrnl resource mapping and reverse teardown only;
- existing firmware lifecycle core: remains disconnected until later codecs
  can provide the complete initdata graph.

No component may call USB proxy code, Python, EL2 per-command services or the
physical framebuffer scraper.

## Failure handling

Every public function returns a typed result.  Invalid versions, geometry,
alignment, containment, ownership, permissions, destination sizes and
allocation failures are permanent failures for that start attempt.

Construction follows validate-then-write semantics.  UAT allocation failure
uses the recorded inventory to free only completed allocations.  Initdata
validation failure leaves the destination entirely unchanged.  Windows MMIO
failure unmaps only active records and resets their fields before returning.
Repeated teardown succeeds without touching hardware twice.

No offline or compile-only failure path changes the stable driver behavior.

## Test gates

### UAT host tests

- golden table and leaf descriptors for every semantic protection;
- first and last legal 39-bit input addresses;
- first and last legal 40-bit output addresses;
- boundary crossings at each level;
- alignment, overflow, overlap and unsupported-permission rejection;
- context-zero firmware ownership and contexts 1-62/63 separation;
- allocation failure at every table level and exact reverse inventory cleanup;
- repeated teardown.

### Initdata host tests

- byte-exact G13/V13_5 golden envelope;
- exact version tuple and three UAT level records;
- derived 40-bit output masks;
- unsupported version, bad pointer, overlapping descriptor, wrong destination
  size and nonzero destination rejection;
- proof that failed validation writes zero bytes.

### Windows and repository tests

- resource tests prove SGX is mapped once and ASC is a contained subview;
- callback failure at every mapping step proves reverse unmap order;
- default package audit proves the transport is unreachable;
- ARM64 WDK builds both default and qualification configurations;
- generated files round-trip without a diff;
- full canonical offline suite remains green.

## Hardware authorization boundary

This specification does not authorize a hardware run.  The first MMIO mapping
experiment requires a separate preregistered row in
`investigation/EXPERIMENTS.md`, one changed variable, a recovery pair, exact
artifact hashes, display-both observability and the existing storage gate.

Pass requires map/subview/unmap receipts, normal Windows responsiveness,
working NVMe/USB/input, zero Event 129, zero critical events and a normal
shutdown.  Any read, write, firmware start, new PnP problem, storage reset or
forced recovery rejects the candidate.

## Acceptance criteria

This milestone is complete when the pure UAT and initdata modules pass their
host tests, both WDK configurations compile, the default stable driver remains
behaviorally unchanged, and the inert mapping transport is ready for a
separately approved hardware qualification.

It is not complete AGX firmware startup and it is not graphics acceleration.
The next architectural increment adds the remaining versioned runtime
structures and wires the complete graph to the lifecycle core.

