# Brandenburg predicate sharing and caching

Follow-up to the empty-tag / atomic-OR experiment in [`README.md`](README.md).
Local branch: `perf/1-tags-filter-bool-predicate-cache`, based on
`perf/1-tags-filter-bool-matching` at `ef57fc9`.

## Implementation

- Intern atomic predicates per filter using their decoded source text as a
  conservative identity. Retain a single shared matcher table; compiled
  expression programs hold predicate IDs, not separate matcher copies.
- Omit repeated identical rules in the existing atomic-OR fast path.
- For general Boolean expressions, cache each predicate's first evaluated
  result lazily within a matching invocation. Preserve the expression order,
  short-circuit behavior, absent/empty/value-inequality semantics, and
  existential matching with duplicate tag keys. No subtree expansion.
- Reusable, explicit contexts belong to the command, not mutable filter state.
  Node/way/relation/area contexts are allocated once after all expressions are
  loaded. Generation stamps avoid clearing the whole cache for every object;
  wraparound clears old stamps before reusing generation 1.
- Cache generations advance per invocation, not per object ID or tag-list
  address. Empty-tag and atomic-OR shortcuts bypass the cache. A context-free
  call or mismatched-size context falls back to allocation-free uncached
  matching. No evaluation-time map lookup, allocation, or cache resizing.
- No changes to libosmium, the classic command, shared matcher utilities,
  reference traversal, output order, or command-line syntax.

## Benchmark reproduction

The runner accepts an optional `--previous` executable, adding the intermediate
empty-tag/atomic-OR implementation to the same shuffled rounds:

```sh
python3 benchmarks/tags-filter-bool/run.py \
  --baseline /path/to/original/osmium \
  --previous /path/to/empty-tag-and-or/osmium \
  --improved /path/to/predicate-cache/osmium \
  --reference /path/to/benchmark-results.json \
  --input /path/to/brandenburg-latest.osm.pbf \
  --classic /path/to/filter-expressions-classic.txt \
  --boolean /path/to/filter-expressions-bool.txt \
  --output-dir /tmp/opencode/brandenburg-predicate-cache-new-run \
  --repeats 3
```

The same input and selector identities as the initial experiment are required.
Original and intermediate binaries were preserved byte-for-byte (their hashes
match the previous experiment). All three implementations use GCC 14.2.0
Release `-O3 -DNDEBUG`, C++17, osmium 1.19.1 / libosmium 2.23.1.

Seven cases per round: classic command/classic file; original Boolean command
with each file; previous optimised Boolean command with each file; new Boolean
command with each file. Three repetitions: **21 full-input invocations**.
Order is shuffled within each round with seed `20261106`. One filter process
at a time, `OSMIUM_POOL_THREADS=1`, `-R`, no progress, regular-file PBF output.
Wall time includes process exit and output generation, excludes hashing.
No concurrent builds, invocation warmups, or cache flush; the input is read
for SHA-256 verification first. These are warm-input-cache observations, not
controlled cold-start measurements. Each output must match the original
benchmark's complete PBF SHA-256 and byte size.

## Results

Measured on 2026-11-06 using the setup above. Implementation commit:
`5649eb33966a92b9b3850bd110e6151cc74f9c37`. All timings below are from this
new interleaved comparison, not the previous experiment's historical timings.

| Implementation | Expressions | Median | Min–max |
| --- | --- | ---: | ---: |
| Original `tags-filter` | Classic | 26.479 s | 26.445–26.489 s |
| Original `tags-filter-bool` | Classic | 62.704 s | 62.513–62.731 s |
| Previous empty-tag/OR version | Classic | 27.628 s | 27.596–27.639 s |
| New sharing/cache version | Classic | 18.549 s | 18.536–18.670 s |
| Original `tags-filter-bool` | Compound | 62.375 s | 62.261–62.523 s |
| Previous empty-tag/OR version | Compound | 41.555 s | 41.397–41.628 s |
| New sharing/cache version | Compound | 33.724 s | 33.680–33.797 s |

Compared with the **previous optimised version**:

- Classic expressions: **1.49× faster**, **32.9% less elapsed time**.
- Compound expressions: **1.23× faster**, **18.8% less elapsed time**.

Compared with the **original Boolean implementation**:

- Classic expressions: **3.38× faster**, **70.4% less elapsed time**.
- Compound expressions: **1.85× faster**, **45.9% less elapsed time**.

On the same classic selector, the new Boolean command takes **29.9% less time**
than the classic command (1.43× speedup). This is a duplicate-heavy rule file,
not evidence that Boolean filtering is intrinsically faster: its OR-only path
**bypasses the cache**, benefiting from rule deduplication. The compound result
measures shared matcher representation and lazy caching together, not their
isolated costs. The classic and compound files select different objects; do
not treat cross-file timings as like-for-like selector comparisons.

All **21/21** output PBFs matched the original benchmark's complete SHA-256 and
byte size:

| Expressions | Bytes | Output SHA-256 |
| --- | ---: | --- |
| Classic | 10,790,090 | `a080e32bfd9a0fef6dc2517615a79ca349ef1038db982e9f79962bf4b7b0b726` |
| Compound | 3,488,658 | `b111351ec75fdc9c3a1f9210c83cccfe1be4fba5a825d63dd8826959e0f666bf` |

See [`predicate-cache-results.json`](predicate-cache-results.json) for all raw
measurements, exact commands, input/binary hashes, and source identities.
Measured binaries, the runner, expression files, and output PBFs are retained
locally under `/tmp/opencode/osmium-bool-predicate-cache/`.

## Validation limits

The Release `osmium` target built successfully. Added regression sources cover
shared predicate counts, decoded quoting, value inequality, cached/uncached
truth-table equivalence, repeated IDs/buffer addresses, copied plans/contexts,
independent contexts, cross-plan reuse, stale context fallback, and duplicate
tag keys. **Tests and pipelines are not run**, preserving the earlier user
restriction. Benchmark output equivalence is not full regression validation,
and these timings do not establish performance for other selectors, datasets,
thread counts, or reference-completion modes.

Subsequent, specifically requested reference-completion checks are recorded
separately in [`reference-validation.md`](reference-validation.md). Those
focused checks do not change the historical test/benchmark scope above.
