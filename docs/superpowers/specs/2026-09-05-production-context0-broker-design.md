# Production context0 broker migration

EXP474 retained-root/management proof is final and is not repeated. User-approved continuation: migrate full current inventory, admit application endpoints, then advance one causal hardware boundary at a time.

## Exact inventory audit

`investigation/EXP475_CONTEXT0_INVENTORY.csv` was emitted by executing current allocation/layout code offline at root6b021e1. It records91 current ranges/20116KiB leaves/97 allocations including7 UAT pages. Source probe is `.local/experiments/EXP475-production-inventory/dump.c`; fake backing addresses are deliberately not represented as hardware evidence.

Migration target:89 Windows backing objects,90 Windows ranges/200 leaves. Legacy Windows crashlog allocation/mapping is removed from production; existing EL2 system crashlog remains. The extra Windows range is the exact buffer-manager alias0x420000000 -> RegionB object10,16KiB. Preserve this existing alias and permissions; do not admit arbitrary low VAs. High Windows range max end0xffffffa00047c000 fits existing broker kernel window. Private firmware prefix range[0xffffff8000000000,0xffffffa000000000) remains opaque EL2/firmware-owned.

Direct/copy sites in current render-admission production: InitdataMemoryPrepare creates roots through AppleAgxUatCreateAddressSpace; legacy MapPrepared calls direct UAT; ImportAndMap copies prefix; backend_platform_windows legacy Create/Publish/Unpublish callbacks publish owned roots, guarded only by qualification macro. All are removed from the full profile's reachable path. Shared legacy test helpers may remain explicitly diagnostic. Context63 memory_runtime_windows publication/residency and backend memory view are separate existing owners, untouched.

Additional pre-initdata gap: native agx/initdata.py build_iomappings(T8103) constructs10 fixed firmware MMIO ranges in kernel IO allocator space, and assigns hwdata.io_mappings. Current Windows RegionB HwdataB allocation is zero-filled and does not yet encode/materialize that native IO-map set. This must be resolved offline before an initdata candidate; it is NOT silently counted as working Windows mappings. The first candidate stops after application endpoint admission, before initdata, so no unresolved initdata/IO semantics are exercised.

## Design

One allocation graph remains. Add broker-only preparation that creates no context0 roots/tables and no Windows crashlog. Its canonical90-range metadata references existing memory objects and VAs. A portable lease journal enumerates200 leaves, translates each owned allocation's existing GuestIpaBase, MAPs/QUERYs through EXP474 API, records exact handles and expected PA, and reverses only its own successes. UNMAP is followed by verified absence, not merely ignored query failure. Allocation release is forbidden until retirement succeeds.

Broker retains original root1 identity and owns root0. Increase bounded record capacity to256 and table capacity24, plus only the exact existing low alias. Introduce an explicit VERIFY_ABSENT operation, protected by epoch and last successful unmap tuple, validating actual absent table state and rejecting stale/private/unowned requests or malformed intermediates. Version the expanded wire ABI coherently; no fallback to old copy API. Existing backing policy/private protection/table implementation unchanged.

Full profile always uses broker preparation/publication. Do not simply remove qualification flag. Mapping retirement must occur after ASC quiescence when full initdata may be consumed; retain original handoff lock ownership while correcting this lifecycle ordering. Endpoint-only qualifier sends start20 and21 through existing native-compatible transport, records advertised endpoints and successful sends, then deliberately stops before initdata. No TA/3D workload on first candidate.

## Gates

Host integration uses the actual production inventory and actual broker/table algorithms:200 leaves plus low alias, exact query, wrong owner/range/generation rejection, all Nth failures reverse only Windows maps, absent readback, prefix/root invariant, second lifetime, and blocked allocation release on partial rollback. Regression full preparation cannot allocate/publish replacement context0 root. Source review and pinned build/hash/sign gates before new hardware. WDK26100 instruction differs from proven28000.2526; clarification requested asynchronously while offline work continues.

Self-review: no replacement roots, no duplicated table subsystem, no context63/IRQ/scheduler change. Current Windows inventory proof is distinct from missing native firmware IO/Hwdata materialization. Hardware success after endpoints is a checkpoint, not a session stop.
