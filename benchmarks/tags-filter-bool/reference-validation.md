# Predicate-cache validation with referenced objects

This follow-up validates reference completion on
`perf/1-tags-filter-bool-predicate-cache`, without changing production code.
The pre-cache and cache executables are the preserved binaries identified in
[`predicate-cache-results.json`](predicate-cache-results.json).

## Scope

- References enabled: **no `-R`**.
- Both existing Brandenburg expression files, with and without
  `--remove-tags`: four scenarios, each run with the previous and cache
  versions, for eight complete PBF outputs.
- Compare complete output bytes using SHA-256 and byte size, not just IDs.
- Run `check-refs -r` on the source PBF and every generated output, including
  node references in ways and node/way/relation members in relations.
- Exercise shared predicates on small fixtures with independent expected
  reference closures and tag retention/removal assertions: positive compound
  matching, inversion, nested/cyclic relations with NOT, area matching, and
  inverted node scope selecting otherwise unscoped ways.
- Run only the ten existing reference-focused integration tests explicitly
  relevant to this validation. No broad unit/full suite or pipelines.

The validation request authorises these focused checks; the earlier
no-test-run restriction was retained for unrelated tests and pipelines.

## Reproduction

```sh
ctest --test-dir build --output-on-failure \
  -R '^tags-filter-bool-(compound|compound-t|ways|area|nested|nested-t|invert|invert-t|invert-node-scope|invert-unscoped-ways)$'

python3 benchmarks/tags-filter-bool/validate-references.py \
  --manifest benchmarks/tags-filter-bool/predicate-cache-results.json \
  --output-dir /tmp/opencode/reference-validation-new-run

python3 benchmarks/tags-filter-bool/check-reference-availability.py \
  --validation-results /tmp/opencode/reference-validation-new-run/results.json \
  --output-dir /tmp/opencode/reference-availability-new-run
```

The validation script verifies input, expression-file, and executable hashes
against the prior manifest. Optional `--previous`, `--improved`, `--input`,
`--classic`, and `--bool` arguments override paths without relaxing identity
checks. The output directory must not already exist.

The five custom fixture scenarios run with both binaries, with and without
tag removal: **20 runs**. Expected object IDs are explicit, while direct-match
sets determine independently which output objects retain tags. Input tags
are canonicalized using `osmium cat` before comparison because OPL supports
different escape spellings for the same tag value. Complete fixture outputs
must also agree between binaries, and every output must pass reference checks.

Brandenburg runs use `OSMIUM_POOL_THREADS=1`, regular-file PBF output, verbose
logging, and no progress display. Timings are single observations to accompany
correctness checks, **not repeated reference-mode benchmarks**.

The source extract is not fully reference-complete: its existing missing
relation members cannot be recovered by filtering. For this reason, the
second script collects missing child-object IDs with `check-refs -r -i` on
the source and outputs. Every unresolved output child ID must also be absent
from the source. This checks that no available reference was omitted, rather
than incorrectly requiring a regional extract to contain outside members.

## Results

The ten existing reference-focused integration tests passed (10/10).
The 20 custom fixture runs also passed: independent IDs and tag checks,
complete output parity, and no missing references, including cycles.

All eight Brandenburg outputs completed successfully, each using **four input
passes**. In all four scenarios, cache and pre-cache outputs were identical
in SHA-256 and byte size, including reference membership, metadata, tag
retention/removal, and object ordering.

| Expressions | Tag removal | Nodes | Ways | Relations | Output SHA-256 |
| --- | --- | ---: | ---: | ---: | --- |
| Classic | No | 3,644,776 | 180,154 | 9,655 | `e69fe4e25805d13b9f6b3c99101f26cdb11abfab3d17f0e314e53d9052cf0c0e` |
| Classic | Yes | 3,644,776 | 180,154 | 9,655 | `6a00e9c0b0bb00a2fb2f2e478cdb2b94521b4a84553be878409249e2397126ef` |
| Compound | No | 1,142,498 | 53,150 | 2,085 | `34bdc928dc8221df1fb1aa52b884bcb189339dc7331736c58339225982bcd983` |
| Compound | Yes | 1,142,498 | 53,150 | 2,085 | `ff8fe15e40fa35454ed88be0f09d4646a151da4962a400c7f7511a3c76b0fe7c` |

### Source completeness caveat

`check-refs -r` returns exit code 1 on both the original input and the filtered
regional outputs because relation members are already absent from the input:

| Missing-reference category | Source | Classic output | Compound output |
| --- | ---: | ---: | ---: |
| Nodes in ways | 0 | 0 | 0 |
| Nodes in relations | 4,481 | 431 | 111 |
| Ways in relations | 199,134 | 11,270 | 10,662 |
| Relations in relations | 3,019 | 128 | 125 |

Output counts agree between binaries and with/without tag removal. An output
from an incomplete source must not be described as globally reference-complete.

The independent `check-refs -r -i` cross-check passed for all eight PBFs:
**every unresolved child-object ID is already absent from the source**. There
were zero missing input-available objects. This establishes correct reference
completion relative to the available source, not availability of outside
members the extract never contained.

## Conclusion and artifacts

Predicate sharing/caching also works with references enabled, including
`--remove-tags`, nesting/cycles, and inverted matching in the focused fixtures.
The four input passes do not reuse predicate results from previous objects or
passes. No production-code changes were required for this validation.

See [`reference-validation-results.json`](reference-validation-results.json)
for the ten-test CTest summary, all 20 custom fixture runs, all eight full-input
commands/output hashes/counts, and the source-availability checks. Local PBFs
and logs are retained under
`/tmp/opencode/osmium-bool-reference-validation-v2/`; missing-ID diagnostics
are under `/tmp/opencode/osmium-bool-reference-availability/`.

No broader test suite or application/CI pipelines were run. This is targeted
validation of the requested mode, not proof over all possible inputs or a
repeated reference-mode performance benchmark.
