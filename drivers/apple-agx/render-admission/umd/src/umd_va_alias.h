/* C53 B0 (D3D12-C53-tiled-buffers-plan.md): tile aliases of slot allocations
 * in reserved GPU VA ranges. An alias maps Pages 4 KiB pages at Va onto the
 * allocation of Token from OffsetPages; it is never the slot's canonical VA.
 * The table must follow every map and free of the ranges so that a canonical
 * replacement (EXP1011) can map the same pages onto the fresh allocation.
 * Pure functions: host-tested by tests/test_umd_va_alias.py. */
#ifndef UMD_VA_ALIAS_H
#define UMD_VA_ALIAS_H

typedef struct _ADMISSION_UMD_VA_ALIAS {
  unsigned long long Token;
  unsigned long long Va;
  unsigned long long OffsetPages;
  unsigned long long Pages;
  unsigned Protection;
} ADMISSION_UMD_VA_ALIAS;

#define ADMISSION_UMD_VA_ALIAS_PAGE 4096ull

/* Entries a cut of [Va, End) adds: one per alias that contains the range. */
static inline unsigned AdmissionUmdVaAliasSplits(const ADMISSION_UMD_VA_ALIAS *Table,
                                                 unsigned Count,
                                                 unsigned long long Va,
                                                 unsigned long long End) {
  unsigned i, splits = 0;
  for (i = 0; i < Count; ++i) {
    const unsigned long long a = Table[i].Va;
    const unsigned long long b = a + Table[i].Pages * ADMISSION_UMD_VA_ALIAS_PAGE;
    if (a < Va && b > End) ++splits;
  }
  return splits;
}

/* Entries the table holds after an alias of [Va, Va + Bytes) is added
 * (before any merge): aliases wholly inside the range are replaced, one
 * containing it splits. 0 for an empty or wrapping range. The caller grows
 * the table to this before the mapping, so that a recorded map never runs out
 * of entries afterwards (C53 B4: the table has no fixed size). */
static inline unsigned AdmissionUmdVaAliasNeed(const ADMISSION_UMD_VA_ALIAS *Table,
                                               unsigned Count,
                                               unsigned long long Va,
                                               unsigned long long Bytes) {
  unsigned i, inside = 0;
  const unsigned long long end = Va + Bytes;
  if ((!Table && Count) || !Bytes || end <= Va) return 0;
  for (i = 0; i < Count; ++i) {
    const unsigned long long a = Table[i].Va;
    const unsigned long long b = a + Table[i].Pages * ADMISSION_UMD_VA_ALIAS_PAGE;
    if (a >= Va && b <= end) ++inside;
  }
  return Count - inside + AdmissionUmdVaAliasSplits(Table, Count, Va, end) + 1u;
}

/* Whether an alias of [Va, Va + Bytes) fits in Capacity entries. */
static inline int AdmissionUmdVaAliasRoom(const ADMISSION_UMD_VA_ALIAS *Table,
                                          unsigned Count, unsigned Capacity,
                                          unsigned long long Va,
                                          unsigned long long Bytes) {
  const unsigned need = Table ? AdmissionUmdVaAliasNeed(Table, Count, Va, Bytes) : 0u;
  return need && need <= Capacity;
}

/* Removes [Va, Va + Bytes) from every alias: an alias inside the range goes,
 * one overlapping an end is trimmed (its OffsetPages advanced past the cut),
 * one containing the range splits in two. Returns 0, leaving the table
 * unchanged, if a split needs more than Capacity entries. */
static inline int AdmissionUmdVaAliasCut(ADMISSION_UMD_VA_ALIAS *Table,
                                         unsigned *Count, unsigned Capacity,
                                         unsigned long long Va,
                                         unsigned long long Bytes) {
  unsigned i;
  const unsigned long long end = Va + Bytes;
  if (!Table || !Count || !Bytes || end < Va ||
      *Count + AdmissionUmdVaAliasSplits(Table, *Count, Va, end) > Capacity) return 0;
  for (i = 0; i < *Count;) {
    ADMISSION_UMD_VA_ALIAS *alias = &Table[i];
    const unsigned long long a = alias->Va;
    const unsigned long long b = a + alias->Pages * ADMISSION_UMD_VA_ALIAS_PAGE;
    if (b <= Va || a >= end) { ++i; continue; }
    if (a < Va && b > end) {
      ADMISSION_UMD_VA_ALIAS tail = *alias;
      tail.Va = end;
      tail.OffsetPages += (end - a) / ADMISSION_UMD_VA_ALIAS_PAGE;
      tail.Pages = (b - end) / ADMISSION_UMD_VA_ALIAS_PAGE;
      alias->Pages = (Va - a) / ADMISSION_UMD_VA_ALIAS_PAGE;
      Table[(*Count)++] = tail;
      ++i;
    } else if (a < Va) {
      alias->Pages = (Va - a) / ADMISSION_UMD_VA_ALIAS_PAGE;
      ++i;
    } else if (b > end) {
      alias->OffsetPages += (end - a) / ADMISSION_UMD_VA_ALIAS_PAGE;
      alias->Pages = (b - end) / ADMISSION_UMD_VA_ALIAS_PAGE;
      alias->Va = end;
      ++i;
    } else {
      Table[i] = Table[--(*Count)];
    }
  }
  return 1;
}

/* Whether Next continues Alias: same allocation and protection, adjacent in
 * VA and in allocation pages. */
static inline int AdmissionUmdVaAliasJoins(const ADMISSION_UMD_VA_ALIAS *Alias,
                                           const ADMISSION_UMD_VA_ALIAS *Next) {
  return Alias->Token == Next->Token && Alias->Protection == Next->Protection &&
         Alias->Va + Alias->Pages * ADMISSION_UMD_VA_ALIAS_PAGE == Next->Va &&
         Alias->OffsetPages + Alias->Pages == Next->OffsetPages;
}

/* Records a new alias, replacing whatever the range mapped before. An alias
 * that continues a neighbour (or is continued by one) merges with it, so a
 * run of tiles mapped onto consecutive heap tiles stays one entry. */
static inline int AdmissionUmdVaAliasAdd(ADMISSION_UMD_VA_ALIAS *Table,
                                         unsigned *Count, unsigned Capacity,
                                         const ADMISSION_UMD_VA_ALIAS *Alias) {
  const unsigned long long bytes = Alias ? Alias->Pages * ADMISSION_UMD_VA_ALIAS_PAGE : 0;
  unsigned i, at;
  if (!Alias || !Alias->Pages || !Alias->Token || !Count ||
      !AdmissionUmdVaAliasRoom(Table, *Count, Capacity, Alias->Va, bytes) ||
      !AdmissionUmdVaAliasCut(Table, Count, Capacity, Alias->Va, bytes)) return 0;
  at = (*Count)++;
  Table[at] = *Alias;
  for (;;) {
    int merged = 0;
    for (i = 0; i < *Count && !merged; ++i) {
      if (i == at) continue;
      if (AdmissionUmdVaAliasJoins(&Table[i], &Table[at])) {
        Table[i].Pages += Table[at].Pages;
        merged = 1;
      } else if (AdmissionUmdVaAliasJoins(&Table[at], &Table[i])) {
        Table[i].Va = Table[at].Va;
        Table[i].OffsetPages = Table[at].OffsetPages;
        Table[i].Pages += Table[at].Pages;
        merged = 1;
      }
      if (merged) {
        /* entry i holds the merged alias: drop the new one (the last entry
         * fills its place) and try the other side with the merged entry */
        Table[at] = Table[--(*Count)];
        at = i == *Count ? at : i;
      }
    }
    if (!merged) break;
  }
  return 1;
}

#endif
