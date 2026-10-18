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
 Busy, ProjectOOM, Exhausted, CoreFailure
};
struct AgxCpuProviderResult {
 AgxCpuProviderStatus status;
 AgxShaderStorageStatus core_status;
 bool core_called;
};
using AgxCpuFailureSink=void(*)(void *,AgxCpuDevice,AgxCpuProviderResult) noexcept;
/* ALL external calls on one provider must be caller-serialized/nonreentrant.
 * The sole allowed internal reentry is synchronous read-only ResolveIdentity
 * from the existing zero-cache core hook during Calculate/Copy. It cannot mutate
 * registry state. Failure sinks must not call provider APIs.
 * Different providers may be independent;
 * this is NOT a thread-safe registry or Windows/runtime lifetime provider.
 * No registry locks exist/are held during core copy or the failure sink.
 * Sinks must not throw, reenter, or mutate source/input/destination buffers.
 * Sink/context stay valid while registered OR any input lease retains device;
 * RetireDevice alone does not end that caller-owned context lifetime.
 * All destination/output records are writable, disjoint from inputs/storage.
 * Failure sink coverage: AcquireInput with a known live device, and failed
 * Calculate/Copy with a known lease. Other APIs return status only. Unknown
 * device/lease/null-output failures have no safely resolved sink.
 * DDI handles are opaque identity values, never dereferenced by this module.
 * Caller owns handle identity lifetime; IDs never reuse, even if handle addresses
 * are retired/re-registered. Library children must be destroyed before device
 * retirement as required by the existing zero-cache module contract.
 */
AgxCpuProvider *AgxCpuProviderCreate();
AgxCpuProviderStatus AgxCpuProviderDestroy(AgxCpuProvider **); /* Busy: leases remain. */
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
/* Only an original CPU fixture supplies zero-cache's real integration hook via
 * this lookup. Membership is compared before any record access. Identity is
 * borrowed while provider/device lease lives; not a production resolver. */
const void *AgxCpuResolveDeviceIdentity(AgxCpuProvider *,D3D12DDI_HDEVICE);
