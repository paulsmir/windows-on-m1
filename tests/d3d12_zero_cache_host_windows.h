/* Original host shim, NOT a Windows ABI model. SPDX-License-Identifier: MIT */
#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
using SIZE_T = std::size_t;
using UINT = uint32_t;
using HRESULT = int32_t;
using VOID = void;
struct D3D12DDI_HDEVICE { void *pDrvPrivate; };
struct D3D12DDI_HPIPELINESTATE { void *pDrvPrivate; };
struct D3D12DDI_HPIPELINELIBRARY { void *pDrvPrivate; };
#define APIENTRY
#define CONST const
#define _In_
#define _Out_
#define S_OK static_cast<HRESULT>(0)
#define E_INVALIDARG static_cast<HRESULT>(0x80070057u)
inline thread_local unsigned agx_host_srw_depth = 0;
struct SRWLOCK { std::mutex mutex; };
#define SRWLOCK_INIT {}
inline void AcquireSRWLockExclusive(SRWLOCK *lock) { lock->mutex.lock(); ++agx_host_srw_depth; }
inline void ReleaseSRWLockExclusive(SRWLOCK *lock) { --agx_host_srw_depth; lock->mutex.unlock(); }
