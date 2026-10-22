/* C53 B2 (D3D12-C53-tiled-buffers-plan.md): the UpdateTileMappings walk, as
 * runs of resource tiles. Microsoft ID3D12CommandQueue::UpdateTileMappings:
 * - regions and ranges are walked together, tile by tile, and the totals
 *   must be equal;
 * - one region may omit its start (all 0) and its size (the whole resource);
 *   with starts but no sizes every region is one tile;
 * - no range flags: every range is sequential heap tiles; no range counts:
 *   one range covers every region tile, several ranges are one tile each;
 * - later regions overwrite earlier ones (runs are emitted in order).
 * C53 T2D (D3D12-C53-tiled-textures.md): a resource is an AGX_TILE_GEOMETRY
 * and runs are in resource tile order (D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_
 * SWIZZLE, D3D12_PACKED_MIP_INFO): per array slice, the standard mips'
 * tiles row by row, then the slice's packed tiles. A region without a box
 * is NumTiles tiles in that order from its start; a box is Height rows of
 * Width tiles in one standard mip. A buffer is one standard mip of one row.
 * Pure: host-tested by tests/test_d3d12_tile_mapping.py. */
#ifndef AGX_D3D12_TILE_MAPPING_H
#define AGX_D3D12_TILE_MAPPING_H

enum {
  AGX_TILE_RANGE_HEAP = 0,   /* sequential heap tiles */
  AGX_TILE_RANGE_NULL = 1,   /* D3D12_TILE_RANGE_FLAG_NULL */
  AGX_TILE_RANGE_SKIP = 2,   /* D3D12_TILE_RANGE_FLAG_SKIP */
  AGX_TILE_RANGE_REUSE = 4,  /* D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE */
};

#define AGX_TILE_MAX_LEVELS 16u

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

/* A 2D resource in standard tiles. Subresource = mip + slice * MipLevels;
 * mips StandardMips.. are packed into PackedTiles tiles per slice. */
typedef struct {
  unsigned MipLevels, ArraySize, StandardMips, PackedTiles;
  unsigned TilesX[AGX_TILE_MAX_LEVELS], TilesY[AGX_TILE_MAX_LEVELS];
} AGX_TILE_GEOMETRY;

/* Tiles of one array slice; 0 for an invalid geometry. */
static inline unsigned long long AgxTileSliceTiles(const AGX_TILE_GEOMETRY *G) {
  unsigned long long tiles = 0;
  unsigned l;
  if (!G || !G->MipLevels || G->MipLevels > AGX_TILE_MAX_LEVELS || !G->ArraySize ||
      G->StandardMips > G->MipLevels || (G->StandardMips < G->MipLevels) != (G->PackedTiles != 0))
    return 0;
  for (l = 0; l < G->StandardMips; ++l) {
    if (!G->TilesX[l] || !G->TilesY[l]) return 0;
    tiles += (unsigned long long)G->TilesX[l] * G->TilesY[l];
  }
  return tiles + G->PackedTiles;
}

/* The resource's tile count; 0 for an invalid geometry or above UINT_MAX. */
static inline unsigned AgxTileResourceTiles(const AGX_TILE_GEOMETRY *G) {
  const unsigned long long slice = AgxTileSliceTiles(G);
  const unsigned long long total = slice * (G ? G->ArraySize : 0u);
  return slice && total / slice == G->ArraySize && total <= 0xffffffffull ? (unsigned)total : 0u;
}

/* Resource tile index of a coordinate (D3D12_TILED_RESOURCE_COORDINATE;
 * for a packed mip, X is the tile within the slice's pack). 0 if outside. */
static inline int AgxTileIndex(const AGX_TILE_GEOMETRY *G, const AGX_TILE_COORD *C,
                               unsigned long long *Index) {
  const unsigned long long slice_tiles = AgxTileSliceTiles(G);
  unsigned long long at = 0;
  unsigned mip, slice, l;
  if (!slice_tiles || !C || C->Z || C->Subresource / G->MipLevels >= G->ArraySize) return 0;
  mip = C->Subresource % G->MipLevels;
  slice = C->Subresource / G->MipLevels;
  for (l = 0; l < G->StandardMips && l < mip; ++l)
    at += (unsigned long long)G->TilesX[l] * G->TilesY[l];
  if (mip >= G->StandardMips) {
    if (C->Y || C->X >= G->PackedTiles) return 0;
    at += C->X;
  } else {
    if (C->X >= G->TilesX[mip] || C->Y >= G->TilesY[mip]) return 0;
    at += (unsigned long long)C->Y * G->TilesX[mip] + C->X;
  }
  *Index = slice * slice_tiles + at;
  return 1;
}

/* Where resource tile Index lives: Packed set (Tile = the tile within the
 * slice's pack) or the standard Mip at tile (X, Y). 0 if outside. */
static inline int AgxTileLocate(const AGX_TILE_GEOMETRY *G, unsigned long long Index,
                                unsigned *Slice, unsigned *Mip, unsigned *X, unsigned *Y,
                                int *Packed) {
  const unsigned long long slice_tiles = AgxTileSliceTiles(G);
  unsigned long long at;
  unsigned l;
  if (!slice_tiles || Index >= slice_tiles * G->ArraySize) return 0;
  *Slice = (unsigned)(Index / slice_tiles);
  at = Index % slice_tiles;
  for (l = 0; l < G->StandardMips; ++l) {
    const unsigned long long level = (unsigned long long)G->TilesX[l] * G->TilesY[l];
    if (at < level) {
      *Mip = l; *X = (unsigned)(at % G->TilesX[l]); *Y = (unsigned)(at / G->TilesX[l]);
      *Packed = 0;
      return 1;
    }
    at -= level;
  }
  *Mip = G->StandardMips; *X = (unsigned)at; *Y = 0; *Packed = 1;
  return 1;
}

/* Calls Emit for each run; returns 0 (nothing more emitted) on a malformed
 * request: unequal totals, a region outside the resource, a box outside its
 * standard mip or on a packed mip or deeper than 1, a box whose NumTiles is
 * not Width x Height, an unknown flag or a heap run past HeapTiles
 * (HEAP/REUSE; HeapTiles 0 = no heap, only NULL/SKIP allowed). Validation
 * runs first, so a malformed request emits nothing. */
static inline int AgxTileMappingWalkGeometry(const AGX_TILE_GEOMETRY *G, unsigned NumRegions,
                                             const AGX_TILE_COORD *Starts,
                                             const AGX_TILE_REGION *Sizes, unsigned NumRanges,
                                             const unsigned *Flags, const unsigned *HeapStarts,
                                             const unsigned *Counts, unsigned HeapTiles,
                                             int (*Emit)(void *, const AGX_TILE_RUN *),
                                             void *Context) {
  const unsigned ResourceTiles = AgxTileResourceTiles(G);
  static const AGX_TILE_COORD origin = {0, 0, 0, 0};
  if (!ResourceTiles) return 0;
  for (int pass = 0; pass < 2; ++pass) {
    unsigned region = 0, region_done = 0, range = 0, range_done = 0;
    unsigned long long region_total = 0;
    if (!NumRegions || !NumRanges || (NumRegions > 1 && !Starts)) return 0;
    for (unsigned i = 0; i < NumRegions; ++i)
      region_total += Sizes ? Sizes[i].NumTiles : Starts ? 1u : ResourceTiles;
    while (region < NumRegions) {
      const AGX_TILE_COORD *start = Starts ? &Starts[region] : &origin;
      const unsigned size = Sizes ? Sizes[region].NumTiles : Starts ? 1u : ResourceTiles;
      const int box = Sizes && Sizes[region].UseBox;
      unsigned long long first;
      unsigned row_tiles = size;
      if (!size || !AgxTileIndex(G, start, &first)) return 0;
      if (box) {
        const unsigned mip = start->Subresource % G->MipLevels;
        const AGX_TILE_REGION *r = &Sizes[region];
        if (mip >= G->StandardMips || r->Depth != 1 || !r->Width || !r->Height ||
            r->Width > G->TilesX[mip] - start->X || r->Height > G->TilesY[mip] - start->Y ||
            (unsigned long long)r->Width * r->Height != size) return 0;
        row_tiles = r->Width;
      } else if (size > ResourceTiles - first) {
        return 0;
      }
      if (region_done == size) { ++region; region_done = 0; continue; }
      if (range >= NumRanges) return 0;
      const unsigned kind = Flags ? Flags[range] : AGX_TILE_RANGE_HEAP;
      const unsigned long long count = Counts ? Counts[range] : NumRanges == 1 ? region_total : 1u;
      if (kind != AGX_TILE_RANGE_HEAP && kind != AGX_TILE_RANGE_NULL && kind != AGX_TILE_RANGE_SKIP &&
          kind != AGX_TILE_RANGE_REUSE) return 0;
      if (range_done == count) { ++range; range_done = 0; continue; }
      /* a run stays inside one box row */
      const unsigned row = region_done / row_tiles, column = region_done % row_tiles;
      const unsigned long long left_row = row_tiles - column, left_range = count - range_done;
      const unsigned n = (unsigned)(left_row < left_range ? left_row : left_range);
      const unsigned long long tile = box ?
          first + (unsigned long long)row * G->TilesX[start->Subresource % G->MipLevels] + column :
          first + region_done;
      AGX_TILE_RUN run = {(unsigned)tile, n, kind, 0u};
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

/* A buffer of ResourceTiles 64 KiB tiles (one row of tiles). */
static inline int AgxTileMappingWalk(unsigned ResourceTiles, unsigned NumRegions, const AGX_TILE_COORD *Starts,
                                     const AGX_TILE_REGION *Sizes, unsigned NumRanges, const unsigned *Flags,
                                     const unsigned *HeapStarts, const unsigned *Counts, unsigned HeapTiles,
                                     int (*Emit)(void *, const AGX_TILE_RUN *), void *Context) {
  AGX_TILE_GEOMETRY buffer = {1u, 1u, 1u, 0u, {ResourceTiles}, {1u}};
  return AgxTileMappingWalkGeometry(&buffer, NumRegions, Starts, Sizes, NumRanges, Flags, HeapStarts,
                                    Counts, HeapTiles, Emit, Context);
}

#endif
