# Shared reverse-reference engine

`parent_lookup.*` is extension-local code used directly by both
`get-parents-extended` and `tags-filter-bool --include-parents`. Neither frontend
launches the other as a subprocess. No libosmium or original command functions
are modified.

## Responsibilities

- `ObjectIds`: sparse, separate typed ID sets; rejects invalid/nonpositive
  seed IDs. Seed identity includes node/way/relation type.
- `ParentFilter`: an independent Boolean parent predicate and its reusable
  evaluation contexts, including expression-level area eligibility. Repeated
  predicates are ORed; no predicate means all way/relation parents qualify.
- `ParentLookup`: checks candidate memberships against a **const frontier**.
  It neither mutates the frontier nor expands newly discovered parents. Its entity
  mask skips source ways when they cannot qualify. Input records need not be
  ordered by relation ID; future relation seeds are known before lookup.
- `find_parents`: breadth-first traversal with `--parent-hops` (default 1,
  non-negative; 0 disables discovery). Each scan uses a frozen frontier. The
  cumulative parent set doubles as visited state; original seeds are already
  expanded at level zero and never expanded again. Matching precedes the visited
  check so sidecars retain cross/cycle edges. A seed found through a cycle is a
  genuine parent, even without `--add-self`. Search stops at the hop limit or
  when no new frontier remains. Parent predicates prune traversal at every hop.
- `Relationships`: optional occurrence-level TSV output. It preserves roles,
  repeated memberships and original member indices, not only unique pairs.
  Multi-hop rows follow hop/source order; each child is expanded once, so each
  traversed member occurrence is emitted once. Depth 0 writes only the header.
- Small I/O helpers: seed-file reading, snapshot rejection and output-path
  collision checks. Original shared matcher/ID utilities are unchanged.

Frontends own output readers/writers, progress/header handling, and policy.
The standalone command preserves its one-pass streaming path at depth 1.
Other depths use shared discovery followed by one ordered output scan. It
writes parents and optionally seed objects, without downward expansion.

The Boolean command keeps its previous path when the new option is absent.
With the option, it first collects all original matches (after inversion),
then discovers ancestors in frozen levels from that set. Only then does it run
downward completion for roots + parents. Parents retain tags as deliberately selected
context; `--remove-tags` affects dependency-only objects. Dependencies never
trigger parent lookup. Parent predicates do not restrict dependencies.

## Current costs and limits

For maximum depth N, shared discovery uses up to N source-reader passes. Empty
or unmatchable frontiers skip discovery; cycles/exhaustion terminate early.
The integrated path adds seed collection and ordered union output: up to N+2
passes with `-R`. Normal downward completion adds up to three relation/member/way
passes: up to N+5 total (three/six at the default depth of 1). Standalone depth 1
still uses one scan; other depths add one output scan to discovery. None of
these hop counts limits downward dependency completion. This is not a fused
single-pass operation. Sparse parent membership does not remove memory used
by the existing downward-reference ID tables.

The Boolean extension now uses `tags_filter_bool::ObjectIdSet` for matching and
downward-reference tables: the existing libosmium `IdSetDense` template with
`chunk_bits=10` (1 KiB chunks covering 8,192 IDs), rather than its default 22
(4 MiB). This is an extension-local instantiation, not a library/default change.
Traversal, cycle detection, tag provenance, and input-pass counts are unchanged.
Smaller chunks reduce sparse bitmap waste but increase allocation count and
pointer-directory size. These are still numeric-range-indexed bitmaps, not
fully sparse sets with memory bounded only by selected-object count.

The new modes require positive-ID snapshots, not history/change inputs.
Standalone source STDIN works at depths 0/1 with independently supplied seeds;
multi-hop standalone and integrated lookup require a replayable source.
Named inputs/OSM output/relationship
output must not alias. Sidecar overwrite follows `--overwrite`, but OSM
`--fsync` does not extend to the TSV. Outputs are not transactional as a pair.

Parent discovery is input-local; it is not geometric containment,
a persistent reverse index, or a promise that regional
input contains every global parent/member. Downward completion can be large
for route/admin parents; restrict parents explicitly where appropriate.

The Naravia pipeline's raw recognition seed policy remains separate from
strict name/wiki-gated admission. Callers must supply the intended seed set;
this engine does not silently substitute expanded geometry objects or follow
reverse hops beyond the explicitly selected depth.

Production validation must preserve parity with unchanged `getparents` for
the unconstrained standalone case and with the composed roots → parents →
`getid -r` workflow for integrated output, including tag removal and cycles.
