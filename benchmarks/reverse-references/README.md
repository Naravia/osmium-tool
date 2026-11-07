# Reverse-reference exploration

The historical exploration below is followed by the shared-engine/command
implementation recorded in [`command-validation.md`](command-validation.md).

Feature branch: `feature/1-tags-filter-bool-reverse-references`, based on the
predicate-cache branch. This is an exploration, not a new installed command.
No libosmium, existing osmium command, shared utility, or pipeline is modified.
The prototypes below use public libosmium APIs without changing that library.

## What “references a filtered object” means

Let **S** be a fixed set of typed IDs of the original selected objects:

- A selected node can have parent **ways and relations**.
- A selected way can have parent **relations**.
- A selected relation can have parent **relations**.
- This is explicit OSM membership, not geographic containment or proximity.
- `n42`, `w42`, and `r42` are different identities.
- Lookup only finds parents present in the supplied source snapshot. A regional
  extract cannot establish that no further parents exist outside that extract.

One reverse hop finds **P(S)**. It does not find a relation referring to a
newly found way, or a grandparent relation, unless the intermediate object is
also in S. Keep S immutable throughout lookup; inserting newly discovered
parents can cause unintended, input-order-dependent recursive selection.

Prefer original/direct matching roots as S, not all objects in a
reference-expanded result. Geometry-support nodes can belong to many other
ways. Asking for their parents is a different and potentially much broader
query, not merely an implementation detail.

There is also a distinction between returning parent **objects** and returning
an explicit child-to-parent **mapping**. A parent OSM record preserves its full
membership list, but a mapping sidecar can directly state which selected child
caused its inclusion, with relation roles and member positions.

## Option 1: existing `getparents` (available now, no code changes)

```sh
osmium tags-filter-bool -R source.osm.pbf \
  -e filter-expressions-bool.txt -o roots.osm.pbf

osmium getparents source.osm.pbf -I roots.osm.pbf -o parents.osm.pbf
```

If the roots file already exists, the second command reads it for IDs and
makes **one source-file pass**, writing parents in source order. `-s` additionally
includes the requested objects themselves, retrieved from the source. Without
`-s`, source nodes need not be materialized; ways are needed only for node
seeds, while relations are always considered. This is entity filtering, not
random access to PBF blocks.

Limitations:

- Exactly one reverse hop; no recursive ancestor option.
- No parent tag/type predicate and no edge sidecar.
- Parent records do not automatically bring their own geometry dependencies.
- The implementation uses three `IdSetDense` seed-ID sets. Its storage is
  chunked, but default chunks are **4 MiB**, each spanning 33,554,432 numeric
  IDs. Sparse IDs across a large numeric range can consume much more memory
  than their count suggests.

For multipolygon-only context, an unchanged-command composition is:

```sh
osmium tags-filter-bool -R parents.osm.pbf 'r/type=multipolygon' \
  -o multipolygon-parents.osm.pbf
```

That extra step scans the smaller parents file, not the original source, but
`getparents` has already spent time checking ways and nonqualifying relations.

Source inspection: `src/command_getparents.cpp:174–228`,
`src/id_file.cpp:21–28`, `man/osmium-getparents.md`;
libosmium `include/osmium/index/id_set.hpp:100–108,198–239`.

## Option 2: isolated, sparse-ID batch scanner (recommended extension direction)

Read known seed IDs into separate node/way/relation lookup sets, then stream
the original source's candidate parent types once. Check each way node or
relation member against the **frozen seed sets**, and write each matching
parent once. This costs O(source reference occurrences) expected work for
hash lookup, with ID storage proportional to seed count rather than ID range.

The exploration-only `parent-scan.cpp` implements two representations:

- **Hash:** standard `std::unordered_set<uint64_t>` per entity type; expected
  constant-time membership, with per-entry/bucket overhead.
- **Sorted:** existing `osmium::index::IdSetSmall` with `sort_unique()` and
  **`get_binary_search()`**, not its linear `get()`. Compact storage, but
  O(log |S|) membership in the inner reference loop.

No all-source reverse index is needed for one batch lookup. Both versions
write ordinary parent OSM records and can optionally stream an edge TSV:

```sh
parent-scan hash source.osm.pbf roots.osm.pbf parents.osm.pbf edges.tsv
```

Columns: `child`, `parent`, `role_percent_escaped`, `member_index` (zero-based).
For ways, the role is empty and the index is the node-reference position.
The file records **occurrences**, retaining repeated membership/closing nodes
and multiple roles. Consumers wanting unique `(child,parent)` pairs must
deduplicate deliberately. Only matched source memberships are emitted; found
parents never become new seeds. With edge output enabled, every membership
must be checked instead of stopping at the first matching child.

For a future production extension, add explicit parent-kind/tag restrictions
before membership checks. A relations-only/multipolygon lookup can skip source
way materialization and all way-node scans. Reuse the Boolean evaluator with
its own context for a parent predicate, without changing the original filter
semantics or the original `getparents` command. This restriction is a proposed
improvement, **not implemented or timed in this prototype**.

Prototype limits: positive-ID snapshots; no history-version policy, negative
IDs, ancestor recursion, downward closure, `--add-self`, or general parent
predicate. This is not a production-ready CLI. No default behavior changes.

## Option 3: integrate only into the Boolean extension

If a single command is wanted, extend only `command_tags_filter_bool.*` and
new extension-local files. Keep libosmium and original commands untouched.

Suggested contract (not implemented): separate `--parents-output=FILE` and
`--parents-expression=EXPR`, explicitly **one hop from direct roots**. Keep
separate root, parent, and geometry-dependency provenance. An optional edge
file can preserve the reason each parent was included without adding custom
OSM tags.

- With `-R`, collect root IDs while writing the current one-pass filter result,
  then perform one parent scan: normally **two original-source passes** total.
  This saves the intermediate seed-file read, not necessarily a source pass.
- With existing downward reference completion, collect actual direct-root IDs
  separately from referenced-only IDs, and add a parent lookup after all root
  IDs are known. Do not mistake the expanded ID set for S. The current
  multi-pass reference traversal cannot simply be repurposed as upward lookup.
- After finding parents, downward expansion is a separate requested policy.
  Generic `getid -r` includes recursive descendants; it is not equivalent to
  “direct members and their way nodes, nested relations as headers only.”

One original-source pass is possible with extra constraints/storage: on a
type-ordered snapshot, node seeds are known before ways and node/way seeds
before relations. Relation seeds can appear **after** relations referring to
them. Defer relation-to-relation candidates and buffer/spool enough parent
records to resolve them after all relation seeds are known. Edge-only output
can defer compact edges instead of full records. Without these provisions,
an apparently single-pass implementation can silently miss parents or become
order-dependent. A simple two-pass design is easier to validate.

## Option 4: reusable reverse index for many queries

For Q repeated lookups on the **same immutable snapshot**, consider a new
sidecar module: sorted typed adjacency arrays, a memory-mapped file, or SQLite
with `(child_type, child_id)` indexing. Store parent identity, and optional role
and occurrence index. Build/bulk-sort once; traverse indexed edges for each
query. Key the artifact by source SHA-256, schema/version, and parent policy.

Tradeoffs:

- A full node-to-way index can dominate storage: even a minimal pair of 64-bit
  IDs costs **16 bytes per edge**, before roles, occurrence indices, type tags,
  directory offsets, allocation overhead, and build-time workspace.
- Indexed IDs do not make a normal PBF randomly accessible. If the query must
  return complete parent objects, it still needs a source scan, a separate
  object-location/PBF-block index, or stored parent records. Do not advertise
  cheap indexed edge lookup as cheap end-to-end PBF extraction.
- Sparse hash tables are useful for **seed membership**; a hash-map-of-vectors
  for **every source node** can be far more expensive than flat adjacency.
- Snapshot changes invalidate the index unless incremental edge updates and
  object retrieval are implemented explicitly. No such cache exists here.

For a one-off batch, prefer the streaming scan. Measure the amortisation
threshold rather than assuming an all-source index is faster.

## Recursive ancestors: smaller relation index or repeated scans

Repeated unchanged `getparents` calls over new frontiers give breadth-first
ancestors, with a visited set for cycles, but require a source scan per level.
Freeze each frontier to give a well-defined hop limit.

A more efficient extension can scan node-to-way memberships for node seeds,
then retain compact relation-to-relation reverse adjacency and traverse it
iteratively in memory. Public `RelationsMapStash::build_member_to_parent_index()`
already supports that relation graph. **`add_members()` only adds relation
members**, not node/way members; it is not a complete node/way parent index.
For relation parents of all member kinds, use separate typed numeric stashes
with `add()` or extension-local typed adjacency. Parent objects still require
buffering/spooling or a subsequent output scan if they were seen earlier.

No recursive traversal or persistent index was implemented/measured here.

## Naravia pipeline compatibility

The adjacent pipeline's current contract requests **one reverse hop from raw
category/active direct-inspection seeds**, for configured multipolygon parents,
then bounded downward materialization. The strict benchmark Boolean file adds
name/wiki admission gates and is **not automatically that raw seed set**.
Likewise, reference-expanded geometry IDs are not context seeds. A tool can
provide efficient lookup, but must not silently substitute these different
selection stages or apply recursive `getid -r` in place of the bounded policy.

Read-only references: `osm_pipeline/prefilter_context.py:73–112` and
`README.md:338–342` in the adjacent pipeline repository. Nothing there changed.

## Build and measurements

Standalone build (substitute the local libosmium include path):

```sh
c++ -O3 -DNDEBUG -std=c++17 -Wall -Wextra -pedantic \
  -isystem /path/to/libosmium/include \
  benchmarks/reverse-references/parent-scan.cpp \
  -o /tmp/opencode/parent-scan -pthread -lz -lbz2 -lexpat
```

`explore.py` compares the unchanged `getparents` command with these prototypes
on the same Brandenburg snapshot and retained seed PBFs from earlier work:

```sh
python3 benchmarks/reverse-references/explore.py \
  --cache-results benchmarks/tags-filter-bool/predicate-cache-results.json \
  --reference-results benchmarks/tags-filter-bool/reference-validation-results.json \
  --prototype /tmp/opencode/parent-scan \
  --output-dir /tmp/opencode/reverse-reference-new-run --repeats 3
```

Requires Linux `os.wait4` for per-child peak RSS. Three shuffled rounds compare
dense/hash/sorted
membership for both direct-root files, plus dense/hash lookup from the expanded
compound file: **24 invocations**. Record per-invocation maximum RSS, complete
output hashes, and wall time including reading seeds and writing parent PBFs.
All outputs for a given seed set must be byte-identical, using a fixed generator
header. Seed generation is not timed again. No warmups or cache flush; input
hashing reads files first. One process at a time, no concurrent builds,
`OSMIUM_POOL_THREADS=1`. These are warm-cache regional observations, not a
planet-scale guarantee. Roles/edge-output overhead and index construction are
not covered by these parent-only timings.

### Measured results

GCC 14.2.0 Release/C++17, libosmium 2.23.1, Linux x86-64. The same Brandenburg
PBF (SHA-256 `c4709c69cbb154db39a4d18d1f96dfb0637377a47053d54c78750393e6d93111`)
and preserved direct/expanded seed files from the preceding experiments were
verified before measurement. Prototype source: `fb1fa9e`. Source/osmium binary,
seed files, prototype binary, complete commands, repetitions, and identities
are recorded in [`results.json`](results.json).

Medians of three runs; peak RSS is measured for each entire child process,
including I/O buffers and executable/library overhead, **not just ID storage**:

| Seed set | Implementation | Time | Peak RSS |
| --- | --- | ---: | ---: |
| Direct classic | Existing `getparents` / dense | 5.011 s | 1,960.6 MiB |
| Direct classic | Prototype hash | 4.089 s | 70.4 MiB |
| Direct classic | Prototype sorted | 4.096 s | 64.7 MiB |
| Direct compound | Existing `getparents` / dense | 4.985 s | 1,882.0 MiB |
| Direct compound | Prototype hash | 4.004 s | 55.5 MiB |
| Direct compound | Prototype sorted | 4.025 s | 59.8 MiB |
| Expanded compound | Existing `getparents` / dense | 5.573 s | 1,955.7 MiB |
| Expanded compound | Prototype hash | 4.777 s | 122.5 MiB |

For direct compound roots, the hash prototype used about **34× less peak RSS**
and **19.7% less elapsed time** than the unchanged command. The classic-root
case used about 28× less peak RSS and 18.4% less elapsed time. Sorted and hash
times are too close to establish a useful speed winner from these three runs.
Although sorted ID storage is compact, total-process RSS also reflects buffer
allocation and is not an isolated comparison of membership-table sizes.

All **24 parent PBFs were byte-identical within their seed-set group** across
implementations and repetitions. These are complete parent records, not only
an ID parity check. Parent PBFs intentionally do not include dependency closure.

### The seed-definition difference is substantial

| Seed set | Seed nodes | Seed ways | Seed relations | Parent ways | Parent relations |
| --- | ---: | ---: | ---: | ---: | ---: |
| Direct classic | 27,513 | 149,884 | 9,643 | 1,627 | 15,596 |
| Direct compound | 7,054 | 36,057 | 2,053 | 291 | 8,560 |
| Expanded compound | 1,142,498 | 53,150 | 2,085 | 162,600 | 13,418 |

The direct compound query returned **8,851 parents**; using its expanded
geometry-support output as seeds returned **176,018 parents** (19.9× as many).
Parent objects may themselves be seeds; these are counts of returned parents,
not necessarily newly added identities. Both queries are valid, but answer
different questions. Make this choice explicit in a future interface.

### Explicit reverse edges

The compound hash prototype was additionally run with edge export. It produced
**51,231 membership occurrences**, representing **50,793 unique child-parent
pairs** and **13,422 distinct seeds with input-local parents**. All edges were
independently cross-checked against XML-decoded seed identities and parent
membership records, including roles and member positions. Its parent PBF
remained byte-identical to the parent-only run. Edge-export time is not part
of the table above.

`examples.py` also exercises typed membership, repeated closing nodes, roles,
forward relation references, cycles, and the distinction between one hop and
ancestors, comparing both prototypes with unchanged `getparents`:

```sh
python3 benchmarks/reverse-references/examples.py \
  --osmium /path/to/osmium --prototype /tmp/opencode/parent-scan \
  --output-dir /tmp/opencode/reverse-reference-examples-new-run
```

All three examples passed for both representations. For example, seeds
`n1,w11,r24` in the existing fixture yield parents `w10,w13,r20,r25`, not the
ancestors of those newly found parents. `r25` correctly finds the earlier
`r24` record. Occurrence-level edges preserve `w11 -> r20`, role `outer`,
member index `0`, and repeated `n1` memberships at different positions.

Artifacts, PBFs, edge TSV, and XML cross-check files remain locally under
`/tmp/opencode/osmium-reverse-references/`. No CTest/full test suite or
application/CI pipelines were run for this exploration. Production integration,
broader-format/version validation, parent predicates, recursion, and persistent
index behavior remain unimplemented.

## Recommendation

1. **Immediate use:** unchanged `getparents -I roots.pbf` for a direct batch
   lookup, if its observed bitmap memory cost is acceptable.
2. **Best next extension:** an independent direct-parent scanner with immutable
   typed sparse seed sets, optional parent predicates, and an edge sidecar.
   Either hash or sorted membership is viable here; do not introduce a new
   library dependency or patch the existing command merely to change its store.
3. **Keep the default bounded:** one reverse hop from explicitly chosen seeds.
   Restrict to relations/multipolygons when that is the actual requirement,
   rather than scanning way nodes unnecessarily. Such a restricted scanner's
   performance still needs measuring.
4. **Only add reverse indexes/ancestor closure when needed:** use a compact
   relation graph for recursive relations; consider an all-source index only
   for repeated queries after accounting for parent-object retrieval costs.

Before production implementation, choose whether the requested output means
parents of direct roots or all expanded objects; arbitrary parents or only
qualifying relations; one hop or ancestors; parent objects, an edge mapping,
or both; and bounded versus recursive downward materialization. No such policy
is silently added by this exploration.
