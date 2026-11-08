# Configurable parent hops

Local feature branch: `feature/1-configurable-parent-hops`, based on
`feature/1-get-parents-extended` at `8206e1e22a3bccc6dd263ac5616619fc6600ac8f`.

## Interface and semantics

Both `get-parents-extended` and `tags-filter-bool --include-parents` accept
`--parent-hops=N`, default **1**. This counts reverse membership edges from
the original seeds/matches, not input passes or downward dependency depth.
Zero disables discovery, two includes grandparents. Only unsigned decimal
integers fitting `std::size_t` are accepted; negative/signed/fractional values
and overflow are argument errors.

Traversal is breadth-first with frozen frontiers and sparse typed visited IDs.
It stops at the requested limit or exhaustion, without repeatedly expanding
cycles. A seed reached through a cycle is a real parent even without `-s`,
but is never expanded twice. Parent predicates apply at every level and prune
paths through nonmatching intermediates. Downward dependencies never become
reverse seeds. Deliberately selected ancestors retain tags with `--remove-tags`.

The optional TSV keeps its four columns. It records traversed reverse member
occurrences in hop/source order, including cross/cycle edges to already visited
parents. Depth zero writes only the header. Downward edges are not recorded.

Standalone depth 1 retains the previous single-pass streaming implementation.
Other depths use up to N discovery passes plus an ordered output scan; depths
greater than 1 require a replayable source. Integrated lookup uses up to N+2
passes with `-R`, or up to N+5 with normal downward completion. Increasing hops
does not optimize the separate dependency passes. No libosmium or original
osmium command implementations were changed.

## Focused validation

- Release GCC 14.2.0/C++17 build of `osmium` and `unit_tests` succeeded.
- **71/71 focused CTest cases passed**, including all existing Boolean
  integration/error/differential cases and 21 new hop cases.
- **10,417 assertions in 25 focused unit cases passed**.
- Checks include depths 0–4, large limits and early exhaustion, relation cycles,
  overlapping original seeds/frontiers, filter pruning, exact TSV edges/order,
  tag provenance, zero-hop standalone STDIN, malformed/overflowing limits, and
  explicit opt-in restrictions. Default and explicit depth 1 produce identical
  OPL and TSV bytes in both interfaces.
- Bash completion syntax and `git diff --check` passed. Manuals and Bash/Zsh
  option lists include the new option.
- No full suite, application pipelines, or CI jobs were run. Pandoc is absent,
  so manual rendering was unavailable. Zsh execution, sanitizer, alternate
  compiler, and install checks were not run. The build lacks optional LZ4.

```sh
cmake --build build --target osmium unit_tests -j2
build/test/unit_tests '[reverse-references],[tags-filter-bool]'
ctest --test-dir build --output-on-failure \
  -R '^(get-parents-extended-|tags-filter-bool-|help_cmd_get-parents-extended$)'
bash -n bash_completion/osmium
git diff --check
```

## Regional compatibility and two-hop composition

The focused reproduction script uses the retained Brandenburg inputs and the
unchanged original executable from the preceding validation. It composes two
levels with original `getparents`, filtering each level with original
`tags-filter`, unions their IDs using original `getid`, and compares that with
standalone `--parent-hops=2`. It also compares integrated two-hop output with
original `getid -r -t` seeded by roots and both parent levels. Default integrated
one-hop PBF/TSV bytes are compared with the previously validated outputs.

**All four byte-for-byte comparisons passed.** The tested parent predicate was
`r/type=multipolygon`, applied at each level. Integrated normal completion with
dependency-only tag removal used six source-reader passes at depth 1 and seven
at depth 2.

| Output | Bytes | SHA-256 |
| --- | ---: | --- |
| Standalone two-hop parents, matching original composition | 74,029 | `506621997800439c54b1fd8b3b1334de2932cea3fd9a56bc9c6459d5ca3b179d` |
| Default integrated one-hop PBF, matching prior validation | 14,432,383 | `ea44a5502a0dcf18708193b43a50484712d7c4fc18a804d3320336bb9ac9defc` |
| Default one-hop relationships TSV, matching prior validation | 86,588 | `27856c1eb265b98fdeb1fd55a69226800772b6cdb4428e7130edf53e9946bcbb` |
| Integrated two-hop PBF, matching original composition | 14,432,744 | `d64bba27c9887b2e431e2825d7d4e924b539023763dc252e7304d6c13f6b7068` |

Tested executable SHA-256:
`d455cfba3488465ec947076946fc1d08c55f5c502d23922c41da62c3dab97cee`.

```sh
python3 benchmarks/reverse-references/validate-parent-hops.py \
  --osmium build/src/osmium \
  --manifest benchmarks/reverse-references/command-validation-results.json \
  --output-dir /tmp/opencode/osmium-parent-hops-validation
```

Use a fresh output directory on subsequent runs. The script uses
`OSMIUM_POOL_THREADS=-2`, the previously recorded operating default. Commands,
input/executable identities, exit codes, timings, pass counts, and output hashes
are retained in that directory's `results.json`. These are correctness checks,
not repeated performance measurements. Regional input omissions remain as
previously documented; original `getid -r` can exit 1 while producing the correct
available-object output.
