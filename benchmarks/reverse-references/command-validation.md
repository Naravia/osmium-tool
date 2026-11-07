# Shared parent lookup: command and integrated-option validation

Local branch: `feature/1-get-parents-extended`, based on the reverse-reference
exploration at `0cb96ac`. Implementation commit:
`8fa0a74a19c05d26b8fee09eec54699bd0e512b0`.

## Delivered

- A reusable in-process engine under `src/reverse_references/` with sparse,
  typed seed sets and frozen one-hop lookup. No subprocess delegation.
- `osmium get-parents-extended`: positional IDs, text/OSM seed files, optional
  self inclusion, parent Boolean predicates, and relationship TSV output.
- `tags-filter-bool --include-parents`: collect original matches after
  inversion, find parents against those frozen IDs, then apply normal downward
  completion to roots + parents. `-R` omits downward dependencies only.
- `--parents-expression` in both frontends: repeated alternatives are ORed,
  with independent cached Boolean evaluation and the existing area semantics.
- `--parent-relationships`: occurrence-level child/parent/role/index sidecar;
  it records seed-to-parent edges, not dependency edges. Parents retain tags
  under `--remove-tags`; dependency-only objects lose them.
- Positive snapshot requirements, history/change rejection, source/seed/output
  alias checks, sidecar overwrite checks, empty-seed handling, manuals,
  registration and shell completions.

No libosmium, original `tags-filter`, original `getparents`, original `getid`,
or original shared matcher/ID/I/O implementations changed. The Boolean path
without the new option retains its existing behavior.

## Focused checks

- Release GCC 14.2.0/C++17 build of `osmium` and `unit_tests` succeeded.
- **50/50 focused CTest cases passed**: new command/help/integration/error
  checks plus all existing Boolean integration/error/differential cases.
- **10,386 assertions across 23 focused unit cases passed**, selected with
  `[reverse-references],[tags-filter-bool]`. Includes the Boolean parser/cache
  truth tables and new typed-seed, parent-filter and setup coverage.
- Bash completion syntax check and `git diff --check` passed.
- No full test suite or application/CI pipelines were run. Pandoc was absent,
  so manual rendering was unavailable. Zsh execution, alternate-compiler,
  sanitizer and install checks were not run. The existing build lacks optional LZ4.

The fixtures exercise direct versus ancestor lookup, parents preceding seed
relations, nested/cyclic dependencies, area qualifiers, repeated parent
predicates, inversion, tag provenance, empty seeds, role/index occurrences,
source STDIN for the standalone interface, invalid IDs/history, and
source/output/sidecar safety checks. Exit codes follow existing tool stages:
setup argument/seed rejection is 2; unsupported source data discovered during
`run()` is a data error, 1.

## Brandenburg parity

Used the retained Brandenburg snapshot and compound expression file from the
preceding experiments. Inputs, retained baseline executable, and output hashes
are verified by [`validate-command.py`](validate-command.py).

1. The new standalone command's entire parent PBF matches the old prototype
   result: SHA-256
   `d3242156f4e834e616519813a7ba3c7a92889801ba107a5fd6c6514bf9014256`.
2. Its entire occurrence TSV matches the independently checked prior edge
   export: SHA-256
   `4e51459315878592917d67457ad15eb8d27897f62cdd54d35aece87000d27e16`.
3. `--parents-expression=r/type=multipolygon` produces the same complete PBF
   as applying the original classic filter to the unconstrained parents.
4. Integrated selection with that parent predicate is byte-identical to an
   independent composition using retained direct roots + qualifying parents
   as `getid` seeds, followed by the original `getid` implementation:

| Mode | Oracle | Output parity | Seed-to-parent TSV parity | Integrated source passes |
| --- | --- | --- | --- | ---: |
| `-R` | `getid -I roots -I parents` | Exact | Exact | 3 |
| Normal | `getid -r -I roots -I parents` | Exact | Exact | 6 |
| `--remove-tags` | `getid -r -t -I roots -I parents` | Exact | Exact | 6 |

The unchanged edge TSV across all modes verifies that downward dependencies
do not become new reverse seeds. All integrated executions exited 0.

The original `getid -r` oracle exits 1 for source-missing relation members, as
expected from the already-diagnosed incomplete regional extract. Its complete
available-object output still matches the integrated command. This does not
claim global reference completeness or retrieval of missing outside members.

### Single-run timings, not a performance benchmark

| Operation | Observed wall time |
| --- | ---: |
| Standalone parents + all occurrence edges | 4.182 s |
| Standalone multipolygon parents + occurrence edges | 2.583 s |
| Integrated roots + parents, `-R` | 41.712 s |
| Integrated roots + parents + dependencies | 51.850 s |
| Same, strip dependency-only tags | 51.797 s |

These operations do different work: standalone starts from existing seed IDs,
while integrated runs include the full Boolean selection and ordered union
output. There were no repetitions or cache controls for these correctness
observations. No end-to-end speedup claim follows from this table.

The first implementation favors a straightforward validated pass structure.
It does not yet fuse scans or remove the existing downward-reference ID tables;
sparse parent lookup alone does not promise the prototype's low RSS for full
dependency-complete extraction.

## Reproduction and artifacts

```sh
cmake --build build --target osmium unit_tests -j2
build/test/unit_tests '[reverse-references],[tags-filter-bool]'
ctest --test-dir build --output-on-failure \
  -R '^(get-parents-extended-|tags-filter-bool-|help_cmd_get-parents-extended$)'

python3 benchmarks/reverse-references/validate-command.py \
  --osmium build/src/osmium \
  --manifest benchmarks/reverse-references/results.json \
  --output-dir /tmp/opencode/new-parent-command-validation
```

[`command-validation-results.json`](command-validation-results.json) retains
commands, binary/input identities, timings, complete hashes, pass counts, and
focused validation summaries. Local PBF/TSV artifacts are retained under
`/tmp/opencode/osmium-parent-command-validation/`. Nothing was pushed for this
development request; the new branch remains local.
