/* Original UNLINKED CPU lease checkpoint. SPDX-License-Identifier: MIT
 * Include actual Windows/d3d12umddi.h before this header. NOT a DDI adapter. */
#pragma once
#include "agx_d3d12_shader_owned_core.h"
struct AgxCpuProvider;
struct AgxCpuDevice { uint64_t value; };
struct AgxCpuRoot { uint64_t value; };
struct AgxCpuInputLease { uint64_t value; };
enum class AgxCpuProviderStatus {
 Ok, Invalid, NotLiveDevice, NotLiveRoot, WrongDevice, RetiredDevice,
 Busy, ProjectOOM, Exhausted, CoreFailure, Reentrant
};
struct AgxCpuProviderResult {
 AgxCpuProviderStatus status;
 AgxShaderStorageStatus core_status;
 bool core_called;
};
using AgxCpuFailureSink=void(*)(void *,AgxCpuDevice,AgxCpuProviderResult) noexcept;
/* Unlinked CPU domain, method-specific concurrency only; NOT a runtime/DDI
 * lifetime provider. Client owns the raw provider entry lifetime: Destroy must
 * not race an unretained entry (notably pre-admission registration). Established
 * leases/operations/scopes/tickets can make a concurrent Destroy probe Busy;
 * actual deletion still requires external caller-entry quiescence. After Destroy
 * success no caller may
 * enter; outstanding leases/operations/scopes/tickets make Destroy Busy.
 *
 * Registry mutations/refcounts are synchronized. Core and sinks run outside
 * registry locks. Same-domain mutation from core/sink reentry returns Reentrant;
 * read-only pinned identity resolution is permitted. Sinks/resolvers do not
 * throw or mutate borrowed input/output. Different providers are independent.
 * All input/output/storage pointers obey the existing readable, stable,
 * writable and disjoint CPU preconditions; this is not pointer validation.
 *
 * RetireDevice closes new device/root/input/operation admissions but preserves
 * known-lease failure reporting, as the serialized935 contract did. Sink/context
 * remain valid until explicit BeginQuiesce + successful TryQuiesce, OR all
 * registrations/leases are destroyed under the legacy serialized contract.
 * BeginQuiesce closes sink admission. Already admitted sink/core work may finish;
 * no callback starts after successful TryQuiesce. This is a CPU client guarantee,
 * not permission to wait in a real DestroyDevice callback.
 * Library children must be destroyed before retirement (existing zero-cache
 * precondition). Borrowed PROGRAM/signature arrays stay stable until successful
 * ReleaseInput; Release is Busy while any operation owns that lease.
 */
AgxCpuProvider *AgxCpuProviderCreate();
AgxCpuProviderStatus AgxCpuProviderDestroy(AgxCpuProvider **); /* Caller entry-quiescence; Busy if pins remain. */
AgxCpuProviderStatus AgxCpuRegisterDevice(AgxCpuProvider *,D3D12DDI_HDEVICE,
 AgxCpuFailureSink,void *context,AgxCpuDevice *);
AgxCpuProviderStatus AgxCpuRetireDevice(AgxCpuProvider *,AgxCpuDevice);
AgxCpuProviderStatus AgxCpuRegisterRoot(AgxCpuProvider *,AgxCpuDevice,
 D3D12DDI_HROOTSIGNATURE,AgxShaderTrustedSpan opaque_canonical_bytes,AgxCpuRoot *);
AgxCpuProviderStatus AgxCpuRetireRoot(AgxCpuProvider *,AgxCpuDevice,AgxCpuRoot);
/* Trusted explicit-span CPU input: args and optional Standard descriptor are
 * readable actual records. PROGRAM/signature arrays remain readable+unchanged
 * until ReleaseLease. This module snapshots args/counts, not those arrays.
 * Canonical root bytes are copied/owned during RegisterRoot, no schema/equality
 * meaning is guessed. Production extraction/readability stays OPEN.
 * Lease can outlive root retirement; new acquisition refuses retired roots.
 * Device retirement invalidates new acquisitions and future Calculate/Copy,
 * while outstanding leases still keep root bytes safe until ReleaseLease.
 */
AgxCpuProviderResult AgxCpuAcquireInput(AgxCpuProvider *,AgxCpuDevice,AgxCpuRoot,
 const D3D12DDIARG_CREATE_SHADER_0010 *,uint32_t stage,
 AgxShaderTrustedSpan program,AgxShaderTrustedSpan input_signature,
 AgxShaderTrustedSpan output_signature,AgxCpuInputLease *);
AgxCpuProviderStatus AgxCpuReleaseInput(AgxCpuProvider *,AgxCpuInputLease);
AgxCpuProviderStatus AgxCpuLeaseRootBytes(AgxCpuProvider *,AgxCpuInputLease,
 AgxShaderTrustedSpan *); /* Borrowed only until this lease release. */
AgxCpuProviderResult AgxCpuCalculate(AgxCpuProvider *,AgxCpuInputLease,AgxShaderOwnedLayout *);
AgxCpuProviderResult AgxCpuCopyOwned(AgxCpuProvider *,AgxCpuInputLease,void *storage,
 SIZE_T capacity,AgxShaderOwned **);
/* LEGACY ONLY: caller-serialized borrowed lookup, unchanged935 semantics.
 * No concurrent lifetime safety is promised for this unpinned return value.
 * New concurrent clients MUST use ResolvePinnedDeviceIdentity instead. */
const void *AgxCpuResolveDeviceIdentity(AgxCpuProvider *,D3D12DDI_HDEVICE);

struct AgxCpuOperation { uint64_t value; };
struct AgxCpuDeviceScope { uint64_t value; };
struct AgxCpuQuiesceTicket { uint64_t value; };
/* One-use Ready->Running->Finished->Ended. End while Running is Busy. Independent
 * operations may execute concurrently; same operation/output is client-owned.
 * Admission before device retirement permits completion after closing. */
AgxCpuProviderResult AgxCpuBeginOperation(AgxCpuProvider *,AgxCpuInputLease,AgxCpuOperation *);
AgxCpuProviderResult AgxCpuCalculatePinned(AgxCpuProvider *,AgxCpuOperation,AgxShaderOwnedLayout *);
AgxCpuProviderResult AgxCpuCopyPinned(AgxCpuProvider *,AgxCpuOperation,void *,SIZE_T,AgxShaderOwned **);
AgxCpuProviderStatus AgxCpuEndOperation(AgxCpuProvider *,AgxCpuOperation *);
AgxCpuProviderStatus AgxCpuOperationRootBytes(AgxCpuProvider *,AgxCpuOperation,AgxShaderTrustedSpan *);
/* Explicit new-client scope around actual zero-cache library calls. Scope Begin/
 * End must occur on the same thread, strict LIFO. No runtime-global setter. */
AgxCpuProviderStatus AgxCpuBeginDeviceScope(AgxCpuProvider *,AgxCpuDevice,AgxCpuDeviceScope *);
AgxCpuProviderStatus AgxCpuEndDeviceScope(AgxCpuProvider *,AgxCpuDeviceScope *);
const void *AgxCpuResolvePinnedDeviceIdentity(AgxCpuProvider *,D3D12DDI_HDEVICE);
AgxCpuProviderStatus AgxCpuBeginQuiesce(AgxCpuProvider *,AgxCpuDevice,AgxCpuQuiesceTicket *);
/* Busy does not consume the ticket; Ok consumes it and closes membership. Live
 * leases may retain root/device memory, but cannot dispatch a new sink afterward.
 * Legacy LeaseRootBytes is borrowed only to successful Release; serialize that
 * raw span's consumption with Release. OperationRootBytes consumption must also
 * finish before that operation is Ended; an operation token is not a second pin. */
AgxCpuProviderStatus AgxCpuTryQuiesce(AgxCpuProvider *,AgxCpuQuiesceTicket *);
