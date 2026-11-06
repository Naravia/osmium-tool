# Brandenburg matching benchmark

This benchmark compares `tags-filter` and the original/optimised
`tags-filter-bool` using the exact Brandenburg PBF and both expression files
from [issue #1](https://github.com/Naravia/osmium-tool/issues/1).

The optimised implementation caches empty-tag results and compiles OR-only
filters into a tag-major `osmium::TagsFilter`. General Boolean expressions
retain their original short-circuit evaluator. There are no libosmium or
classic-command changes, predicate memoization, or reference-pass changes.

## Reproduction

Use Python 3.11 or newer and two Release builds with identical compiler flags.
Preserve the baseline binary from commit
`d93bfcbd6b8f54e2620b95b5f16998a5e840c40a` before rebuilding the optimised
sources. Both executable basenames must remain `osmium`, in different
directories; other names can be interpreted as subcommands by osmium.

Download the original `benchmark-results.json` and both expression files
attached to issue #1, and retain the exact input identified below. Then run:

```sh
python3 benchmarks/tags-filter-bool/run.py \
  --baseline /path/to/baseline/osmium \
  --improved /path/to/improved/osmium \
  --reference /path/to/benchmark-results.json \
  --input /path/to/brandenburg-latest.osm.pbf \
  --classic /path/to/filter-expressions-classic.txt \
  --boolean /path/to/filter-expressions-bool.txt \
  --output-dir /tmp/opencode/brandenburg-matching-new-run \
  --repeats 3
```

The output directory must not already exist. It retains all generated PBFs
and a `results.json` containing exact commands, input/binary identities,
timings, output hashes, and medians. The script stops on any command failure
or mismatch against the original PBF output hashes and sizes.

## Method

- Three measured invocations per case, shuffled within each complete round
  using seed `20261106`: 15 full-input filter invocations in total.
- One filter process at a time; `OSMIUM_POOL_THREADS=1`; `-R`; progress disabled.
- PBF output to regular temporary files. Wall time includes output generation
  and process exit, but excludes subsequent SHA-256 computation.
- No invocation warmups and no cache flush. Input identity verification reads
  the PBF before the runs, so these are warm-input-cache observations, not
  controlled cold-start measurements. Host scheduling/storage variation is
  not eliminated by three repetitions.
- Require byte-identical PBFs against the original benchmark, not just equal
  object counts. Historical timings are context only; use newly interleaved
  baseline measurements for speedup calculations.
- Build only the `osmium` target. No test suite or application/CI pipeline is
  executed as part of this benchmark. Added regression tests are not run.

## Input identities

| Input | SHA-256 |
| --- | --- |
| Brandenburg, 296,211,267 bytes | `c4709c69cbb154db39a4d18d1f96dfb0637377a47053d54c78750393e6d93111` |
| Classic expressions, 7,713 bytes | `eaa2e9f109afeec29444e699406d270f16e6f6ced7639a2d354c85f9295ac48e` |
| Compound expressions, 15,498 bytes | `eec4c4d01d699929b61f4063036b0960ed668d3e4522eece74766229cf605c01` |

Brandenburg snapshot: `2026-10-18T20:25:06Z`, replication sequence `4937`.

## Results

Measured on 2026-11-06, using GCC 14.2.0 Release (`-O3 -DNDEBUG`, C++17),
osmium 1.19.1 / libosmium 2.23.1, Linux x86-64. Baseline commit:
`d93bfcbd6b8f54e2620b95b5f16998a5e840c40a`; optimised implementation:
`c297984`, local branch `perf/1-tags-filter-bool-matching`.

| Implementation | Expressions | Median | Min–max |
| --- | --- | ---: | ---: |
| Baseline `tags-filter` | Classic | 26.676 s | 26.663–26.965 s |
| Baseline `tags-filter-bool` | Classic | 62.525 s | 62.308–62.644 s |
| Optimised `tags-filter-bool` | Classic | 27.566 s | 27.547–27.587 s |
| Baseline `tags-filter-bool` | Compound | 61.989 s | 61.981–62.204 s |
| Optimised `tags-filter-bool` | Compound | 41.519 s | 41.475–41.674 s |

- Classic expressions: **2.27× faster**, **55.9% less elapsed time** than the
  original Boolean command; **3.3% slower** than classic `tags-filter`.
- Compound expressions: **1.49× faster**, **33.0% less elapsed time** than the
  original Boolean command on the same compound file. Its selector differs
  from the classic file, so comparisons between those files are not
  like-for-like selector comparisons.
- These measurements evaluate the two optimisations together, not an
  individual-cost attribution. Mixed-expression tag rescans remain; predicate
  interning/memoization was deliberately deferred.

All **15/15** complete output PBFs matched the original benchmark's bytes and
SHA-256. Original object counts therefore also remain unchanged:

| Expressions | Nodes | Ways | Relations | Bytes | Output SHA-256 |
| --- | ---: | ---: | ---: | ---: | --- |
| Classic | 27,513 | 149,884 | 9,643 | 10,790,090 | `a080e32bfd9a0fef6dc2517615a79ca349ef1038db982e9f79962bf4b7b0b726` |
| Compound | 7,054 | 36,057 | 2,053 | 3,488,658 | `b111351ec75fdc9c3a1f9210c83cccfe1be4fba5a825d63dd8826959e0f666bf` |

See [`results.json`](results.json) for all raw measurements, exact commands,
binary hashes, and input identities. Original PBF outputs and measured binaries
are retained locally under `/tmp/opencode/osmium-bool-performance/`.
The measurement loop ran from a temporary copy of `run.py`; the checked-in
runner additionally rejects executable names other than `osmium` up front.

The Release `osmium` target built successfully. Regression cases were added
for empty-tag semantics, atomic OR equivalence, duplicate tags/rules, copying
filters, and switching to mixed-expression fallback. **Tests and pipelines
were not run**, preserving the earlier restriction. Benchmark output equality
does not replace full regression validation or establish performance for
other selectors, datasets, thread counts, or reference-completion modes.
