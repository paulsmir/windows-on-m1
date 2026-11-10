/* SPDX-License-Identifier: MIT
 * rc118 receipt-only: data dependencies between consecutive submitted
 * batches (measure-batch-deps), to size GPU overlap. The KMD runs one job at
 * a time; overlapping a compute job with a render job (as Metal does on the
 * same GPU) is only possible for a pair whose buffers do not conflict.
 *
 * Each batch's Mesa BO handles (AGX_BATCH_FOREACH_BO_HANDLE) are hashed into
 * a read set (every referenced BO) and a write set (the BOs the batch is the
 * Mesa writer of). A batch depends on an earlier one when it reads what the
 * earlier one wrote (RAW), writes what it read (WAR) or writes what it wrote
 * (WAW). Sets are 4096-bit Bloom filters, so a dependency may be reported
 * that does not exist (never the reverse): the receipt is an upper bound on
 * dependencies and a lower bound on independent pairs.
 *
 * Plain C, no CRT headers beyond stdint, valid C and C++.
 */
#ifndef AGX_WIN32_BATCH_DEPS_H
#define AGX_WIN32_BATCH_DEPS_H
#include <stdint.h>

#define AGX_WIN32_BATCH_DEPS_WORDS 64u   /* 4096 bits per set */
#define AGX_WIN32_BATCH_DEPS_HISTORY 4u

typedef struct AGX_WIN32_BATCH_DEPS_ENTRY {
   uint64_t Read[AGX_WIN32_BATCH_DEPS_WORDS];
   uint64_t Write[AGX_WIN32_BATCH_DEPS_WORDS];
   unsigned Kind, Reads, Writes;
} AGX_WIN32_BATCH_DEPS_ENTRY;

typedef struct AGX_WIN32_BATCH_DEPS {
   AGX_WIN32_BATCH_DEPS_ENTRY History[AGX_WIN32_BATCH_DEPS_HISTORY];
   AGX_WIN32_BATCH_DEPS_ENTRY Current;
   unsigned Count, Next;
} AGX_WIN32_BATCH_DEPS;

static inline unsigned
AgxWin32BatchDepsBit(unsigned handle)
{
   uint32_t h = (uint32_t)handle * 2654435761u;
   return (unsigned)(h >> 20) & (AGX_WIN32_BATCH_DEPS_WORDS * 64u - 1u);
}

static inline void
AgxWin32BatchDepsBegin(AGX_WIN32_BATCH_DEPS *d, unsigned kind)
{
   unsigned i;
   for (i = 0; i < AGX_WIN32_BATCH_DEPS_WORDS; ++i)
      d->Current.Read[i] = d->Current.Write[i] = 0u;
   d->Current.Kind = kind;
   d->Current.Reads = d->Current.Writes = 0u;
}

static inline void
AgxWin32BatchDepsAdd(AGX_WIN32_BATCH_DEPS *d, unsigned handle, int write)
{
   const unsigned bit = AgxWin32BatchDepsBit(handle);
   const uint64_t mask = 1ull << (bit & 63u);
   d->Current.Read[bit >> 6] |= mask;
   ++d->Current.Reads;
   if (write) {
      d->Current.Write[bit >> 6] |= mask;
      ++d->Current.Writes;
   }
}

static inline int
AgxWin32BatchDepsConflict(const AGX_WIN32_BATCH_DEPS_ENTRY *later,
                          const AGX_WIN32_BATCH_DEPS_ENTRY *earlier)
{
   unsigned i;
   for (i = 0; i < AGX_WIN32_BATCH_DEPS_WORDS; ++i)
      if ((later->Read[i] & earlier->Write[i]) ||
          (later->Write[i] & earlier->Read[i]))
         return 1; /* RAW, or WAR/WAW (Write is a subset of Read) */
   return 0;
}

/* Bit k set: the current batch conflicts with the (k + 1)th previous one;
 * bit 8 + k: that previous batch exists; bits 16 + 4k: its kind. Pushes the
 * current batch into the history. */
static inline unsigned
AgxWin32BatchDepsCommit(AGX_WIN32_BATCH_DEPS *d)
{
   unsigned k, mask = 0u;
   for (k = 0; k < AGX_WIN32_BATCH_DEPS_HISTORY && k < d->Count; ++k) {
      const unsigned slot = (d->Next + AGX_WIN32_BATCH_DEPS_HISTORY - 1u - k) %
                            AGX_WIN32_BATCH_DEPS_HISTORY;
      if (AgxWin32BatchDepsConflict(&d->Current, &d->History[slot]))
         mask |= 1u << k;
      mask |= 1u << (8u + k);
      mask |= (d->History[slot].Kind & 0xfu) << (16u + 4u * k);
   }
   d->History[d->Next] = d->Current;
   d->Next = (d->Next + 1u) % AGX_WIN32_BATCH_DEPS_HISTORY;
   if (d->Count < AGX_WIN32_BATCH_DEPS_HISTORY)
      ++d->Count;
   return mask;
}

/* N4c-v4 receipt (measure-batch-deps-exact, only with the OV2 cross-lead
 * feature): the exact BO handle lists of the current and the previous
 * submitted batch next to the Bloom sets, so a Bloom conflict can be told
 * from a real one. Reads holds every handle of the batch, Writes those it is
 * the Mesa writer of; a list that fills sets Overflow (1 reads, 2 writes),
 * after which conflicts are a lower bound. */
#define AGX_WIN32_BATCH_EXACT_READS 256u
#define AGX_WIN32_BATCH_EXACT_WRITES 64u
#define AGX_WIN32_BATCH_EXACT_SHOWN 4u
#define AGX_WIN32_BATCH_EXACT_VALUES 11u

typedef struct AGX_WIN32_BATCH_EXACT_LIST {
   unsigned Read[AGX_WIN32_BATCH_EXACT_READS];
   unsigned Write[AGX_WIN32_BATCH_EXACT_WRITES];
   unsigned Reads, Writes, Overflow, Valid;
} AGX_WIN32_BATCH_EXACT_LIST;

typedef struct AGX_WIN32_BATCH_EXACT {
   AGX_WIN32_BATCH_EXACT_LIST Previous, Current;
} AGX_WIN32_BATCH_EXACT;

static inline void
AgxWin32BatchExactBegin(AGX_WIN32_BATCH_EXACT *e)
{
   e->Current.Reads = e->Current.Writes = e->Current.Overflow = 0u;
   e->Current.Valid = 1u;
}

static inline void
AgxWin32BatchExactAdd(AGX_WIN32_BATCH_EXACT *e, unsigned handle, int write)
{
   AGX_WIN32_BATCH_EXACT_LIST *c = &e->Current;
   if (c->Reads < AGX_WIN32_BATCH_EXACT_READS)
      c->Read[c->Reads++] = handle;
   else
      c->Overflow |= 1u;
   if (!write)
      return;
   if (c->Writes < AGX_WIN32_BATCH_EXACT_WRITES)
      c->Write[c->Writes++] = handle;
   else
      c->Overflow |= 2u;
}

static inline int
AgxWin32BatchExactHas(const unsigned *list, unsigned count, unsigned handle)
{
   unsigned i;
   for (i = 0; i < count; ++i)
      if (list[i] == handle)
         return 1;
   return 0;
}

/* values {kind, exact conflicts with the previous batch, the first
 * AGX_WIN32_BATCH_EXACT_SHOWN conflicting handles, their directions (1 the
 * current batch writes what the previous one read, 2 it reads what the
 * previous one wrote, 3 both wrote), overflow (current Overflow | previous
 * Overflow << 2 | 16 no previous batch)}; unused slots 0. Then the current
 * batch becomes the previous one. */
static inline void
AgxWin32BatchExactCommit(AGX_WIN32_BATCH_EXACT *e, unsigned kind,
                         uint32_t values[AGX_WIN32_BATCH_EXACT_VALUES])
{
   const AGX_WIN32_BATCH_EXACT_LIST *c = &e->Current, *p = &e->Previous;
   unsigned i, n = 0u;
   for (i = 0; i < AGX_WIN32_BATCH_EXACT_VALUES; ++i)
      values[i] = 0u;
   values[0] = kind;
   for (i = 0; p->Valid && i < c->Reads; ++i) {
      const unsigned h = c->Read[i];
      const int cw = AgxWin32BatchExactHas(c->Write, c->Writes, h);
      const int pw = AgxWin32BatchExactHas(p->Write, p->Writes, h);
      unsigned direction = 0u;
      if (cw && pw)
         direction = 3u;
      else if (pw)
         direction = 2u;
      else if (cw && AgxWin32BatchExactHas(p->Read, p->Reads, h))
         direction = 1u;
      if (!direction)
         continue;
      if (n < AGX_WIN32_BATCH_EXACT_SHOWN) {
         values[2u + n] = h;
         values[2u + AGX_WIN32_BATCH_EXACT_SHOWN + n] = direction;
      }
      ++n;
   }
   values[1] = n;
   values[2u + 2u * AGX_WIN32_BATCH_EXACT_SHOWN] =
      c->Overflow | (p->Overflow << 2) | (p->Valid ? 0u : 16u);
   e->Previous = e->Current;
}

#endif
