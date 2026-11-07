/* Windows user-mode SHA-256 backend for CPU-only library validation.
 * SPDX-License-Identifier: MIT. Unlinked from the UMD until the importer exists.
 */
#ifndef AGX_D3D12_LIBRARY_WIRE_CNG_H
#define AGX_D3D12_LIBRARY_WIRE_CNG_H
#ifdef _WIN32
#include <bcrypt.h>
#include <climits>
#include <cstring>
#include "agx_d3d12_library_wire_digest.h"

class AgxD3d12CngSha256 {
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
public:
    AgxD3d12CngSha256() = default;
    ~AgxD3d12CngSha256() { if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0); }
    AgxD3d12CngSha256(const AgxD3d12CngSha256 &) = delete;
    AgxD3d12CngSha256 &operator=(const AgxD3d12CngSha256 &) = delete;
    bool Init() {
        if (algorithm_) return true;
        BCRYPT_ALG_HANDLE handle = nullptr;
        if (BCryptOpenAlgorithmProvider(&handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            return false;
        ULONG length = 0, returned = 0;
        if (BCryptGetProperty(handle, BCRYPT_HASH_LENGTH,
                              reinterpret_cast<PUCHAR>(&length), sizeof(length),
                              &returned, 0) < 0 || returned != sizeof(length) ||
            length != 32) {
            BCryptCloseAlgorithmProvider(handle, 0);
            return false;
        }
        algorithm_ = handle;
        return true;
    }
    static bool Digest(const AgxD3d12WireSpan *spans, std::size_t count,
                       std::uint8_t out[32], void *context) {
        if (out) std::memset(out, 0, 32);
        auto *self = static_cast<AgxD3d12CngSha256 *>(context);
        if (!self || !self->algorithm_ || !out || (count && !spans)) return false;
        BCRYPT_HASH_HANDLE hash = nullptr;
        if (BCryptCreateHash(self->algorithm_, &hash, nullptr, 0, nullptr, 0, 0) < 0)
            return false;
        bool ok = true;
        for (std::size_t i = 0; i < count; ++i) {
            if ((!spans[i].data && spans[i].bytes) || spans[i].bytes > ULONG_MAX ||
                BCryptHashData(hash, const_cast<PUCHAR>(spans[i].data),
                               static_cast<ULONG>(spans[i].bytes), 0) < 0) {
                ok = false;
                break;
            }
        }
        if (ok) ok = BCryptFinishHash(hash, out, 32, 0) >= 0;
        BCryptDestroyHash(hash);
        if (!ok) std::memset(out, 0, 32);
        return ok;
    }
};
#endif
#endif
