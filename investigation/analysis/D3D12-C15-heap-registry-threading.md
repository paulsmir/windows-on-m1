# Synchronize per-device heap registry metadata

Prepared while EXP1573 executes frozen source `a1e01c91`. This proposal changes
neither that package nor its recovery controller.

## WINDOWS CONTRACT:

The already inspected Microsoft [DDI threading specification](https://github.com/microsoft/DirectX-Specs/blob/71e431fa0f713818c6a3a66f49ab0e6732712d15/d3d/CPUEfficiency.md)
permits concurrent device/resource methods. See the earlier
[owner threading review](D3D12-C15-owner-threading-contract.md). Its unresolved
callback reentry question applies to a whole native transaction, not to these
three metadata-only functions. Current `RegisterHeap`, `UnregisterHeap`, and
`HeapAt` directly share plain `heap_count` and `heaps[]` without synchronization.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Valid concurrent registrations
of distinct live heaps must retain every registry entry. Removing one live heap
must not corrupt unrelated entries or race concurrent lookup. Use actual three
function bodies with live host-owned heap records and ThreadSanitizer; no invalid
addresses, native submission, stale objects, or adversarial inputs.

## AGX/ASAHI CONTRACT:

The registry resolves an existing GPU VA to its already created native buffer;
its metadata update does not initialize hardware or call Gallium. Native create
finishes before RegisterHeap; UnregisterHeap finishes before owner destruction.
HeapAt returns a borrowed heap under the caller's existing valid object lifetime.
A registry lock does not prove owner publication, context serialization, resource
retirement, or callback reentry safety. Those distinct gates remain open.

## TRANSLATION:

If the real-source concurrency test confirms the race, add one per-device metadata
SRW lock, initialized in actual CreateDevice. Writers exclusively lock count and
array changes; lookup uses a shared lock until copying its result/offset. Release
before returning, including misses. No native or runtime callback, diagnostics,
allocation, wait for owner state, or destruction under that lock. Preserve current
capacity and lookup ranges. Do not add a global device transaction lock.

Microsoft [SRW lock documentation](https://learn.microsoft.com/en-us/windows/win32/sync/slim-reader-writer--srw--locks)
defines dynamic initialization and shared/exclusive operations; these locks are
not recursive. The three functions contain no callback/reentry path. Existing
PSO publication already uses the same Windows synchronization API. Native WDK
compilation separately checks actual structure/private-size integration; host
stand-ins for unused device fields do not establish the native ABI.

## WHAT IS STILL UNKNOWN:

EXP1574 reproduced an actual ThreadSanitizer data race in concurrent valid
registrations. The narrow metadata lock passes the same concurrent test and
36 focused checks; EXP1575 passes all three native WDK compile variants.
EXP1576 ran 1639 host tests: its 124 failure/error IDs exactly match the recorded
baseline union, with no added IDs and unchanged tested inputs. This does not
make the whole suite green. Native package/private-device sizing and later
normal GPU regressions still require their own gates. No current J313 failure is attributed to
this source-level gap. No complete free-threaded native owner claim follows.

## Checkpoint and recovery

CPU actual-source RED/GREEN and valid lifetime/lookup controls, existing device
initialization/error-owner tests, then native WDK compilation. A later fresh-device
resource-creation step must keep heaps alive through joins and GPU fences and
record its own receipts. Ordinary exact package rollback remains mandatory.
