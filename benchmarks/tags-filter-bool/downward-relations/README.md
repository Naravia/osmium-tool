# Bounded downward relation expansion: archive snapshot

Archive of `feature/6-bounded-downward-relations`, implemented for
[issue #6](https://github.com/Naravia/osmium-tool/issues/6) and merged through
[PR #8](https://github.com/Naravia/osmium-tool/pull/8).

- Implementation commit: `f4be92c4c65327bfcee46e76d2d4db7c46378667`.
- Merge into `custom-main`: `be36f94acc072dcc8126c819d7c1ed2d6b47001c`.
- Annotated archive tag: `archive/feature/6-bounded-downward-relations`.

This archive-only commit adds benchmark evidence on top of the original feature
tip. It is deliberately **not** merged into `custom-main`; the tag preserves the
feature history and benchmark files after deletion of the development branch.
This follows the existing `archive/feature/*` and `archive/perf/*` snapshots.

## Feature and validation

`tags-filter-bool --downward-relation-hops=N` bounds relation-member discovery
independently of reverse parent depth. Every intentionally selected relation gets
the full budget; available way nodes are completed without an extra relation
hop. Frozen breadth-first frontiers handle cycles, forward references, selected
nested roots, and overlapping paths without allocating a whole recursive closure.
Relation headers/member lists remain unchanged at the boundary. The recursive
traversal remains the unchanged default; combining a bound with `-R` is rejected.

The Release/C++17 build succeeded and **452/452 CTest cases passed**, including
synthetic expected-identity and source-object checks, option validation, parent
lookup interactions, tag provenance, and PBF/TSV byte comparisons. The full suite
was rerun successfully immediately before the feature commit. No pipeline run,
sanitizer run, alternate compiler check, or new benchmark was performed while
creating this archive.

## Authorized benchmark

See [report.md](report.md) for three alternating pairs on the retained Brandenburg
PBF, measured on 2026-11-08. Only `--downward-relation-hops=1` differs between
variants. Both use the same executable, Boolean expressions, two reverse parent
hops, multipolygon-only parents, dependency tag removal, and
`OSMIUM_POOL_THREADS=-2` (two workers on the recorded four-logical-CPU host).

Median wall time: **43.409 s recursive vs 42.520 s bounded** (2.0% less).
Median peak RSS: **214.82 MiB recursive vs 224.23 MiB bounded** (4.4% more).
Bounded output has 1,925 fewer objects and is 16,916 bytes smaller. Reverse TSV
bytes are unchanged. The recursive PBF and reverse TSV match the saved
pre-feature Brandenburg outputs. These warm-cache local observations are not a
general performance guarantee or a claim of equivalent reference completeness.

The measurement preceded the feature commit, so the original metadata records
base `b7796ec` with a modified working tree. `tracked-source.diff` is the recorded
tracked diff; the resulting implementation and synthetic fixtures are preserved
by the feature commit in this tag's ancestry. Original measurements, timestamps,
absolute paths, hashes, and version strings have not been rewritten.

An earlier attempted synthetic pre-change comparison copied a wrapper rather
than an independent binary. It did **not** establish pre-change parity and is
not evidence for this archive. The Brandenburg comparison against saved prior
PBF/TSV hashes and the committed default/legacy synthetic comparisons are valid.

## Files and reproduction

- `run.py`: original driver, copied byte-for-byte as executed. It contains the
  original environment-specific paths, refuses to reuse its output directory,
  and reads the prior paired-benchmark manifest from
  `/tmp/opencode/osmium-dependency-chunks/two-hop-paired/results.json`.
- `previous-benchmark-results.json`: that prior manifest, retained for input
  identity and saved-output comparison provenance.
- `filter-expressions-bool.txt`: exact expression file used.
- `results.json`: exact commands, identities, individual timings, peak RSS, CPU
  usage, output hashes/sizes, pass counts, counts, and summary statistics.
- `report.md`: original human-readable report.
- `*-fileinfo.json`: complete object-count/metadata inspection for each variant.
- `*.stderr.log`: original six measured command logs. Their verbose memory
  figures are virtual-memory statistics, not the measured process peak RSS.
- `artifacts.json`: names, sizes, and SHA-256 hashes of every original run
  artifact, including files intentionally not stored in Git.

As in the previous archive snapshots, generated PBFs and raw relationship TSVs,
the source PBF, and compiled executables are **not** committed. Their identities
are recorded, and the original run outputs remain under
`/tmp/opencode/osmium-issue6-downward-brandenburg/` on the measurement host.
Empty stdout logs are omitted. No files are removed from that directory.

To reproduce on another host, use the recorded source PBF identity and included
expressions, point the original driver at the desired actual executable
(`build/src/osmium`, not its wrapper), restore or adapt the prior manifest path,
and select a fresh output directory. Equivalent measured filter invocations are:

```sh
OSMIUM_POOL_THREADS=-2 "$OSMIUM" tags-filter-bool "$INPUT" \
  -e "$EXPRESSIONS" --include-parents --parent-hops=2 \
  --parents-expression=r/type=multipolygon --remove-tags \
  --no-progress --verbose --generator=reverse-reference-exploration \
  --parent-relationships="$EDGES" -o "$OUTPUT"
# Bounded variant: add --downward-relation-hops=1.
```

Run sequential pairs in recursive/bounded, bounded/recursive,
recursive/bounded order. The original driver measures elapsed wall time and
Linux `wait4.ru_maxrss`, hashes every output, and performs object inspection
outside the timed filter invocations. Input hashing warms the cache; no cache
flush or explicit invocation warmup was used. Rebuilding can change executable
hashes and embedded version strings; do not edit the historical measurements to
make them describe a different executable or run.
