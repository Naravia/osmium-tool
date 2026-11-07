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
- `ParentLookup`: checks candidate memberships against a **const seed set**.
  It neither mutates seeds nor expands newly discovered parents. Its entity
  mask skips source ways when they cannot qualify. Input records need not be
  ordered by relation ID; future relation seeds are known before lookup.
- `Relationships`: optional occurrence-level TSV output. It preserves roles,
  repeated memberships and original member indices, not only unique pairs.
- Small I/O helpers: seed-file reading, snapshot rejection and output-path
  collision checks. Original shared matcher/ID utilities are unchanged.

Frontends own readers/writers, progress/header handling, and policy. The
standalone command reads seeds first and makes one source pass; it writes
parents and optionally seed objects, without downward expansion.

The Boolean command keeps its previous path when the new option is absent.
With the option, it first collects all original matches (after inversion),
then discovers parents from that frozen set. Only then does it run downward
completion for roots + parents. Parents retain tags as deliberately selected
context; `--remove-tags` affects dependency-only objects. Dependencies never
trigger parent lookup. Parent predicates do not restrict dependencies.

## Current costs and limits

The straightforward integrated path uses three source reader passes with
`-R`: seed collection, parent discovery, and ordered union output. Empty or
unmatchable seed sets can skip parent discovery. Normal downward completion
can add relation/member/way passes (up to six source reader passes with the
current nested-relation traversal). It is not advertised as a faster fused
single-pass operation. Sparse parent membership does not remove memory used
by the existing downward-reference ID tables.

The new modes require positive-ID snapshots, not history/change inputs.
Standalone source STDIN works with independently supplied seeds; integrated
lookup requires a replayable source. Named inputs/OSM output/relationship
output must not alias. Sidecar overwrite follows `--overwrite`, but OSM
`--fsync` does not extend to the TSV. Outputs are not transactional as a pair.

Parent discovery is one hop and input-local; it is not geometric containment,
an ancestor traversal, a persistent reverse index, or a promise that regional
input contains every global parent/member. Downward completion can be large
for route/admin parents; restrict parents explicitly where appropriate.

The Naravia pipeline's raw recognition seed policy remains separate from
strict name/wiki-gated admission. Callers must supply the intended seed set;
this engine does not silently substitute expanded geometry objects or
recursively expand reverse hops.

Production validation must preserve parity with unchanged `getparents` for
the unconstrained standalone case and with the composed roots → parents →
`getid -r` workflow for integrated output, including tag removal and cycles.
