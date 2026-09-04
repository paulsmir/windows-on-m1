# J313 AGX firmware-runtime handoff

## Purpose

The full-owner m1n1 profile may establish the minimum AGX firmware execution
environment before Windows starts.  It does not own Windows allocations,
contexts, queues, submissions, fences, presentation, or completion after the
handoff.  Windows remains the owner of those operations.

## Producer: m1n1

The producer may publish `READY` only after all of these facts are true:

1. `gfx-asc` and `sgx` PMGR domains are active.
2. The exact J313 SGX preparation read/write has completed.
3. ASC is running and the firmware has emitted a management HELLO.
4. The handoff region is initialized and unlocked.
5. Context-0 roots identify one live firmware address space.

The producer must persist a generation, the exact context-0 pair, the handoff
physical range, and an owner state.  It must not publish opaque host pointers,
channel ring addresses, or a mutable allocator object to Windows.

## Consumer: Windows

Windows accepts the handoff only when its magic, ABI version, size, generation,
owner state, J313 physical ranges, handoff state, and context-0 TTBR pair all
validate.  It then adopts the live firmware state and must not issue a second
ASC RUN, management bootstrap, or context-0 root publication.

Windows owns all subsequent endpoint setup, initdata, queue publication,
submission and completion.  A failed validation is cold-start fail-closed; it
never partially adopts the producer.

## Reverse cleanup

Windows must first quiesce its endpoint/queue work.  It then marks the handoff
released.  m1n1 is the only layer permitted to destroy the retained firmware
execution environment and power down the domains.  A failed release retains
the state for reset; no layer may reuse its generation.

## First hardware discriminator

`READY` plus a nonzero generation must be visible to Windows before driver
binding.  After one natural bind, the durable receipt must distinguish:

`handoff invalid` / `handoff adopted` / `first inherited RTKit message` /
`post-adoption endpoint failure`.
