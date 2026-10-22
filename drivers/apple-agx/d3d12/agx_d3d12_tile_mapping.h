/* C53 B2 (D3D12-C53-tiled-buffers-plan.md): the UpdateTileMappings walk for
 * reserved buffers, as runs of resource tiles. Microsoft
 * ID3D12CommandQueue::UpdateTileMappings:
 * - regions and ranges are walked together, tile by tile, and the totals
 *   must be equal;
 * - one region may omit its start (all 0) and its size (the whole resource);
 *   with starts but no sizes every region is one tile;
 * - no range flags: every range is sequential heap tiles; no range counts:
 *   one range covers every region tile, several ranges are one tile each;
 * - later regions overwrite earlier ones (runs are emitted in order).
 * Buffers have one subresource and tiles along X only. Pure: host-tested by
 * tests/test_d3d12_tile_mapping.py. */
#ifndef AGX_D3D12_TILE_MAPPING_H
#define AGX_D3D12_TILE_MAPPING_H

enum {
  AGX_TILE_RANGE_HEAP = 0,   /* sequential heap tiles */
  AGX_TILE_RANGE_NULL = 1,   /* D3D12_TILE_RANGE_FLAG_NULL */
  AGX_TILE_RANGE_SKIP = 2,   /* D3D12_TILE_RANGE_FLAG_SKIP */
  AGX_TILE_RANGE_REUSE = 4,  /* D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE */
};

typedef struct {
  unsigned ResourceTile; /* first resource tile of the run */
  unsigned Tiles;
  unsigned Kind;         /* AGX_TILE_RANGE_* */
  unsigned HeapTile;     /* HEAP: first heap tile; REUSE: the shared tile */
} AGX_TILE_RUN;

typedef struct {
  unsigned X, Y, Z, Subresource;
} AGX_TILE_COORD;

typedef struct {
  unsigned NumTiles, UseBox, Width, Height, Depth;
} AGX_TILE_REGION;

/* Calls Emit for each run; returns 0 (nothing more emitted) on a malformed
 * request: unequal totals, a region outside the buffer, a box that is not a
 * row of tiles, an unknown flag or a heap run past HeapTiles (HEAP/REUSE;
 * HeapTiles 0 = no heap, only NULL/SKIP allowed). Validation runs first, so
 * a malformed request emits nothing. */
static inline int AgxTileMappingWalk(unsigned ResourceTiles, unsigned NumRegions, const AGX_TILE_COORD *Starts,
                                     const AGX_TILE_REGION *Sizes, unsigned NumRanges, const unsigned *Flags,
                                     const unsigned *HeapStarts, const unsigned *Counts, unsigned HeapTiles,
                                     int (*Emit)(void *, const AGX_TILE_RUN *), void *Context) {
  for (int pass = 0; pass < 2; ++pass) {
    unsigned region = 0, region_done = 0, range = 0, range_done = 0;
    unsigned long long region_total = 0;
    if (!NumRegions || !NumRanges || (NumRegions > 1 && !Starts)) return 0;
    for (unsigned i = 0; i < NumRegions; ++i)
      region_total += Sizes ? Sizes[i].NumTiles : Starts ? 1u : ResourceTiles;
    while (region < NumRegions) {
      const unsigned start = Starts ? Starts[region].X : 0u;
      const unsigned size = Sizes ? Sizes[region].NumTiles : Starts ? 1u : ResourceTiles;
      if (Starts && (Starts[region].Y || Starts[region].Z || Starts[region].Subresource)) return 0;
      if (Sizes && Sizes[region].UseBox &&
          (Sizes[region].Height != 1 || Sizes[region].Depth != 1 || Sizes[region].Width != size)) return 0;
      if (!size || start > ResourceTiles || size > ResourceTiles - start) return 0;
      if (region_done == size) { ++region; region_done = 0; continue; }
      if (range >= NumRanges) return 0;
      const unsigned kind = Flags ? Flags[range] : AGX_TILE_RANGE_HEAP;
      const unsigned long long count = Counts ? Counts[range] : NumRanges == 1 ? region_total : 1u;
      if (kind != AGX_TILE_RANGE_HEAP && kind != AGX_TILE_RANGE_NULL && kind != AGX_TILE_RANGE_SKIP &&
          kind != AGX_TILE_RANGE_REUSE) return 0;
      if (range_done == count) { ++range; range_done = 0; continue; }
      const unsigned long long left_region = size - region_done, left_range = count - range_done;
      const unsigned n = (unsigned)(left_region < left_range ? left_region : left_range);
      AGX_TILE_RUN run = {start + region_done, n, kind, 0u};
      if (kind == AGX_TILE_RANGE_HEAP || kind == AGX_TILE_RANGE_REUSE) {
        if (!HeapStarts) return 0;
        const unsigned long long heap = (unsigned long long)HeapStarts[range] + (kind == AGX_TILE_RANGE_HEAP ? range_done : 0u);
        if (heap + (kind == AGX_TILE_RANGE_HEAP ? n : 1u) > HeapTiles) return 0;
        run.HeapTile = (unsigned)heap;
      }
      if (pass && kind != AGX_TILE_RANGE_SKIP && !Emit(Context, &run)) return 0;
      region_done += n;
      range_done += n;
    }
    /* every range tile was used: trailing empty ranges are allowed */
    for (; range < NumRanges; ++range, range_done = 0) {
      const unsigned long long count = Counts ? Counts[range] : NumRanges == 1 ? region_total : 1u;
      if (range_done != count) return 0;
    }
  }
  return 1;
}

#endif
