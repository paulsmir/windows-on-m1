/* SPDX-License-Identifier: MIT
 * rc103: the D3D12 owner's level-upload ring (agx_d3d12_windows.cpp
 * level_upload, agx_win32_asahi_level_upload.h).
 *
 * One persistent CPU-visible buffer, created like an UPLOAD heap
 * (AgxD3d12OwnerCreateBuffer), holds the tiled levels that the compute
 * copies read. Spans are handed out in order and never reused until a
 * drain: when the next level does not fit, the owner submits and waits for
 * all of its work (AgxD3d12OwnerSubmitAndWait) and starts again at 0. So no
 * CPU write ever overlaps a span a recorded or running copy still reads,
 * with no per-span fence bookkeeping.
 *
 * APPLE_AGX_D3D12_LEVEL_UPLOAD (read once per process): exactly "0" keeps
 * every upload on the C70 staging blit (one render pass per level); unset
 * or anything else enables whole-level uploads.
 *
 * Header-only plain C, valid C and C++ (host tests, x86 /TP).
 */
#ifndef AGX_D3D12_LEVEL_RING_H
#define AGX_D3D12_LEVEL_RING_H

/* rc112 (EXP2057a): GravityMark FHD 100k streams 208 MiB of levels inside
 * its scored window; a 16 MiB ring drained 18 times there, each drain a
 * SubmitAndWait of about 150 ms (2.7 s of the 9.7 s frame 26->27 stall).
 * 64 MiB on 64-bit processes; a WOW64 (x86) process keeps 16 MiB of its
 * address space. */
#define AGX_D3D12_LEVEL_RING_BYTES \
  ((sizeof(void *) >= 8 ? 64ull : 16ull) * 1024ull * 1024ull)
#define AGX_D3D12_LEVEL_RING_ALIGN 256ull

/* Where a level of `bytes` goes in a ring of `capacity` bytes whose first
 * `used` bytes are taken: 0 at *offset (= used); 1 at offset 0 after a
 * drain; -1 never (empty, larger than the ring, or inconsistent input). */
static inline int AgxD3d12LevelRingPlace(unsigned long long capacity,
    unsigned long long used, unsigned long long bytes,
    unsigned long long *offset) {
  if (!offset || !bytes || !capacity || bytes > capacity || used > capacity ||
      (used & (AGX_D3D12_LEVEL_RING_ALIGN - 1ull)))
    return -1;
  if (bytes <= capacity - used) {
    *offset = used;
    return 0;
  }
  *offset = 0ull;
  return 1;
}

/* The ring's used bytes after a level of `bytes` placed at `offset`. */
static inline unsigned long long AgxD3d12LevelRingAdvance(
    unsigned long long offset, unsigned long long bytes) {
  return (offset + bytes + AGX_D3D12_LEVEL_RING_ALIGN - 1ull) &
         ~(AGX_D3D12_LEVEL_RING_ALIGN - 1ull);
}

/* The switch value as read by GetEnvironmentVariableA into a 2-byte
 * buffer: `length` is its return value. Only exactly "0" disables. */
static inline int AgxD3d12LevelUploadEnabled(const char *value,
                                             unsigned long length) {
  return !(length == 1ul && value && value[0] == '0');
}
#endif
