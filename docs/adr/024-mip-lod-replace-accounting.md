# ADR 024 — Replacing lod 0 drops the mip chain; lod bytes follow replacements

## Status

Accepted

## Context

`CacheManager::putMip(lod == 0)` stored the full raster in the Viewer pool and
left any Preview-pool lod ≥ 1 chain in place. A later full image for the same
key could then be paired with mips built from the previous raster.
`putMemory(FullImage)` already drops that chain on every successful store.

For lod ≥ 1, `MipChainInfo::bytes` always added `img.byteSize()`, including
when the same lod key replaced an entry already in the Preview pool.
`trimMipsToBudget` spends that total, so a replacement counted both the old
and the new payload and could evict chains too early.

Metadata, Thumbnail, Preview, and Viewer remain separate `ImageCache` pools.
This decision does not merge those budgets.

## Decision

- A `putMip(baseKey, 0, img)` that leaves `img` in the Viewer pool then calls
  `eraseMips(baseKey)`. If the Viewer put refuses the entry (larger than the
  Viewer budget) the previous chain is left alone.
- `putMip` for lod ≥ 1 subtracts the previous Preview entry's stored size
  (0 when absent) and adds the size actually stored after `put`. A refused
  oversized put does not change the tracked total. `trimMipsToBudget` still
  reads `MipChainInfo::bytes`.

## Consequences

- Replacing lod 0 cannot leave stale preview mips. Callers that want a chain
  for the new full image call `ensureMips` or `getBestMip` after the lod 0 put.
- Replacing one lod no longer inflates that chain's byte total. Other lods of
  the same key stay in the total.
- Preview and Viewer budgets are unchanged. Mip pixels still live in the
  Preview pool and still share that pool's eviction with other previews.
