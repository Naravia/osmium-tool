# Smaller dependency bitmap chunks

Local performance branch: `perf/1-tags-filter-bool-dependency-chunks`, based on
`feature/1-configurable-parent-hops` at `fa01737`.

## Change and scope

`tags_filter_bool::ObjectIdSet` instantiates the existing libosmium
`IdSetDense<unsigned_object_id_type, 10>` template. The Boolean extension's
matching and referenced-ID tables use this alias: **1 KiB chunks covering 8,192
numeric IDs**, instead of the default **4 MiB chunks covering 33,554,432 IDs**.

Only storage granularity changes. Traversal, relation-cycle detection, parent
depth, tag provenance, source ordering, and source-pass counts are unchanged.
The tuning applies to `tags-filter-bool` with and without `--include-parents`.
Standalone parent lookup already uses sparse IDs and is unchanged. No original
osmium commands, shared original tool utilities, or libosmium files/defaults
were modified. No new library dependency or command-line option is introduced.

This is not fully sparse storage: the bitmap pointer directory still depends
on the largest numeric ID. Smaller chunks also mean more allocations. The
regional results below do not establish a universal optimum for planet-scale,
dense, or arbitrarily large-ID inputs.

## Focused validation

- GCC 14.2.0 Release/C++17 build of `osmium` and `unit_tests` passed.
- **74/74 focused CTest cases passed**, including the existing Boolean
  differential checks and all configurable-parent-hop cases.
- **10,452 assertions in 27 focused unit cases passed**.
- New tests check the 8,191/8,192 chunk boundary, old 33,554,431/33,554,432
  boundary, scattered IDs above 32 bits, duplicate insertion, ordered iteration,
  moving/clearing tables, exact initial chunk allocations, dependency completion,
  cyclic relations, and tag provenance with and without parent lookup.
- `git diff --check` passed. No full suite, application/CI pipeline, sanitizer,
  alternate-compiler, or installation checks were run. Pandoc is absent, so
  manual rendering was unavailable. The build lacks optional LZ4.

```sh
cmake --build build --target osmium unit_tests -j2
build/test/unit_tests '[reverse-references],[tags-filter-bool]'
ctest --test-dir build --output-on-failure \
  -R '^(get-parents-extended-|tags-filter-bool-|help_cmd_get-parents-extended$)'
git diff --check
```

## Brandenburg measurements

Same retained positive-ID Brandenburg snapshot and compound expressions as the
preceding validations. Four logical CPUs; `OSMIUM_POOL_THREADS=-2` uses two pool
workers. Both binaries used the identical configuration:

```sh
OSMIUM_POOL_THREADS=-2 "$OSMIUM" tags-filter-bool "$INPUT" \
  -e "$EXPRESSIONS" --include-parents --parent-hops=2 \
  --parents-expression=r/type=multipolygon --remove-tags \
  --parent-relationships="$EDGES" --verbose --no-progress \
  --generator=reverse-reference-exploration -o "$OUTPUT"
```

Normal downward completion is enabled; both PBF and TSV writing are timed.
Both versions use **seven source-reader passes**. Three sequential paired
rounds alternated order: baseline/optimized, optimized/baseline,
baseline/optimized. No concurrent builds/filter jobs. Input identity hashing
warms the input cache; no cache flush or invocation warmups. Wall time includes
process exit and excludes subsequent output hashing. Peak resident memory is
per-process Linux `wait4.ru_maxrss`, not the verbose tool's virtual `VmPeak`.

| Round | Baseline time | 1 KiB time | Baseline peak RSS | 1 KiB peak RSS |
| --- | ---: | ---: | ---: | ---: |
| 1 | 44.785 s | 43.815 s | 2,163.8 MiB | 215.2 MiB |
| 2 | 44.575 s | 43.898 s | 2,162.4 MiB | 214.9 MiB |
| 3 | 44.758 s | 43.856 s | 2,165.0 MiB | 214.8 MiB |
| **Median** | **44.758 s** | **43.856 s** | **2,163.8 MiB** | **214.9 MiB** |

Median peak RSS fell **90.1%** (about 10.1× smaller), saving approximately
**1,948.9 MiB**. Median elapsed time fell **2.0%** (0.903 s). The principal
benefit is memory, not a large end-to-end speedup; tag filtering still dominates
runtime. These are three warm-input observations on one regional workload,
not a universal performance guarantee.

### Identity and correctness

- Input: 296,211,267 bytes, SHA-256
  `c4709c69cbb154db39a4d18d1f96dfb0637377a47053d54c78750393e6d93111`.
- Expressions: 15,498 bytes, SHA-256
  `eec4c4d01d699929b61f4063036b0960ed668d3e4522eece74766229cf605c01`.
- Baseline binary: 3,018,936 bytes, SHA-256
  `d455cfba3488465ec947076946fc1d08c55f5c502d23922c41da62c3dab97cee`.
- Optimized binary: 3,019,752 bytes, SHA-256
  `795b84fe1bade14440a6541f9af6daec1cc65e767a5895e27e9eceeee1b147e7`.
- All six PBFs are **byte-identical** to the validated two-hop composition:
  14,432,744 bytes, SHA-256
  `d64bba27c9887b2e431e2825d7d4e924b539023763dc252e7304d6c13f6b7068`.
- All six TSVs are **byte-identical** to the validated two-hop relationships:
  86,611 bytes, SHA-256
  `934203c5090f2034e3fd7417a557265114fe28e92a53f87883fa8db4e4e6c865`.
- Every measured command exited 0. As before, regional omissions are not
  fetched externally and output parity is not a claim of global completeness.

## Reproduction and artifacts

Before rebuilding, retain the pre-change executable under a separate directory
with basename **`osmium`**; command dispatch treats other executable names as
multicall commands. The retained manifest must describe that baseline binary.

```sh
python3 benchmarks/reverse-references/measure-dependency-chunks.py \
  --baseline /tmp/opencode/osmium-dependency-chunks/baseline/osmium \
  --osmium build/src/osmium \
  --manifest /tmp/opencode/osmium-parent-hops-validation/results.json \
  --output-dir /tmp/opencode/osmium-dependency-chunks/two-hop-paired \
  --parent-hops=2 --runs=3
```

Use a fresh output directory when repeating. The script verifies input/binary
identities and PBF/TSV parity for every invocation. Exact absolute commands,
full timings, CPU usage, peak RSS, hashes, and individual stderr logs are retained
under that output directory; the complete record is `results.json`. The script
also accepts `--parent-hops=1` to measure the backward-compatible default mode.
