# R153 manager lifetime — authorized offline implementation

Base 1b9db94d; existing integration/ad04-windows-compiler linked worktree.
R153 explicitly requests derivation and implementation; no further design or
hardware approval sought. User rejects receipt-only EXP867. No package/Air.

## Contract and sources

Asahi queue/mod.rs Queue::new constructs Buffer using VM allocators, while its
vertex/fragment WorkQueues have independent ring/event lifetimes. buffer.rs
BufferInner owns Info, block control, counter, stats and backing lists/heap;
new_scene creates a Scene/user buffer and retains Buffer. Scene::drop releases
the hardware manager slot after the last scene. render.rs emits InitBuffer
on scene.rebind or TVB growth, before the vertex command. workqueue.rs signal
retires commands only after their stamp value completes. m1n1 GPUBufferManager
(context.py) keeps info/control/counter/lists; GPURenderer owns the instance
and initialized flag with separate workqueues. Buffer::increment increments
its usage counter per committed render. G13 firmware structs are fw/buffer.rs.

R137's Windows owner is ADMISSION_G3_PROCESS.PrivateManager, shared by contexts
with matching generation; scratch belongs to the private scene. BeginJob pins
that manager/scene, serializes one active native job and leases slot1. Joined
completion precedes GraphEndJob and scene reporting/reclaim. Asahi's storage
objects therefore map to a per-manager CPU snapshot plus the existing one
serialized context0 materialization slot. No allocation/ACPI/caps/m1n1 change.

The saved snapshot owns objects1 (Info),20 (BlockControl),21 (Counter),22
(Stats); scene13 is fresh each job. Queue objects, EventControl, stamps and
R151 sequence remain separate. Restore snapshots only while the provider is
Created/RuntimeReady, no pending fence. Save only on joined successful
completion, after coherent CPU reads and before releasing the process lease.
Keys include process owner, manager generation and backing addresses/size;
a root change forces slot rebind but does not destroy manager-owned counters.
Different manager generation/backing starts fresh state. A→B→A restores A.
Failures retain the current private-scene quarantine; no fabricated abort or
hardware-stop success. Legacy/B1 behavior remains covered.

## Steps / independently reviewable commits

1. Portable manager snapshot/materializer and idle InitBM selection. RED→GREEN
   real materializer: changed pointers + preserved queue/event bytes, A→B→A,
   same VA/different owner, new generation, same manager/new scene, counter
   preservation, busy/stale completion refusal and InitBM head accounting.
2. Connect process-owned state to actual BeginJob, worker planning, joined
   completion/save and manager retirement. Real source integration replay;
   compile all affected ARM64 TUs /W4 /WX /analyze. Full suite compared against
   same-environment baseline; no new failing identities. Final source hash.
3. Fresh whole-change review, targeted fixes if needed; implementation commits
   and CHANGES rows with resulting full hashes, implemented status only.

## Atomic contract / next hardware checkpoint

A binding transition must atomically select owner/backing, restore manager
state, refresh process pointers/scene, plan InitBM and its extra TA entry, and
preserve queue state. Dynamic staging must permit InitBM at later queue
sequences, preserving fresh stamp derivation. These fields are one ownership correction. EXP867
proposed single variable = this fix atop exact866 source17ee8894, unchanged
R143 and Flush excluded. Require fresh TA+3D completion across manager switch,
then existing600s/nonzero scanout checkpoints. Recovery remains EXP866
hiddenCode45 dump-first exact cleanup then ordinary Code28. No run authorized.

Unknowns: hardware effectiveness of the derived correction and any later GPU
fault. No derivable ownership/lifetime item is delegated to hardware.
