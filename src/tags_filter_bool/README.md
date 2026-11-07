# Maintaining tags-filter-bool

This fork adds a standalone command. The original `command_tags_filter.*`,
shared matcher utilities, and `tags-filter` documentation/tests are unchanged.

## Implementation boundary

- `expression.*`: Boolean tokenizer, recursive-descent parser, flat-owned
  expression tree, and short-circuit evaluator. Atomic predicates use the
  existing `get_tag_matcher()` helper without changing its semantics.
- `../command_tags_filter_bool.*`: CLI, file reader, object/area matching, and
  reference traversal adapted from upstream `command_tags_filter.*` at commit
  `b5dcfd5`. This is intentionally independent rather than a refactor of the
  original command. Retain the upstream license headers.
- `../../test/tags-filter-bool/`: parser/setup tests, independent expected-ID
  and reference-integrity checks, and full-output differential tests against
  the original command for equivalent predicates.
- `../../man/osmium-tags-filter-bool.md`: user-visible syntax and limits.

The new command always scans ways during inverted reference completion, even
when only node expressions were supplied. Inversion selects otherwise
unscoped ways too, and those ways must include their nodes. The original
command is not changed.

## Matching optimisations

Each expression caches its result for an empty tag list at construction;
each filter caches the OR of those results. Empty-tag matching therefore
does not depend on expression count, including when NOT selects untagged
objects. Value inequality still requires an existing tag.

Filters containing only atomic predicates and OR nodes also compile an
`osmium::TagsFilter` using the existing libosmium API. Matching walks the
tags once rather than restarting a tag scan for every predicate. Adding
an AND/NOT expression switches the entire filter back to the original
left-to-right short-circuit evaluator and releases the compiled rules.
Node, way, relation, and area filters maintain these plans independently.
Neither path allocates during matching or changes reference traversal.

Each filter now interns predicates by their decoded source text. Its expression
programs reference a single matcher table instead of retaining per-expression
matcher copies. The atomic-OR fast path also omits repeated identical rules.
This is conservative deduplication, not canonicalisation of equivalent syntax.

For mixed expressions the command supplies an explicit `Filter::Context`,
allocated once after setup for each node/way/relation/area filter. Evaluation
is lazy and left-to-right; the first visit to a predicate scans the tags and
caches its Boolean result, and further visits in that matching invocation
reuse it. Generation stamps invalidate results on every cached invocation,
including repeated object IDs, buffer addresses, and input passes. Wraparound
clears stamps before reusing generation 1. Empty tags and atomic ORs bypass
the per-predicate cache entirely.

`Filter` itself has no mutable evaluation state. Independent contexts may be
used with the same immutable filter. The context-free overload remains
uncached and allocation-free; a mismatched context size falls back to that
overload rather than allocating during matching. Recreate contexts after
adding expressions to restore caching if the predicate count changed.

## Updating upstream

The optional `--include-parents` mode uses the shared extension-local engine
described in [`../reverse_references/README.md`](../reverse_references/README.md).
Keep its frozen-root, parent/dependency provenance and no-subprocess guarantees
when updating the copied reference traversal. Without that option the prior
filtering path remains intact.

1. Rebase the feature branch onto the desired upstream version. Integration
   edits are additive: command/source registration, unit-test registration,
   manual registration/install manifest, and Bash/Zsh completion entries.
2. Review upstream changes to `command_tags_filter.cpp` and its header since
   the previous update, especially reference traversal, inversion, tag
   removal, and I/O handling. Relevant fixes must be ported explicitly; a
   copied implementation does not receive them automatically.
3. Check changes to `get_tag_matcher()` and libosmium's tag matching APIs.
4. Build using C++17 and `CMAKE_BUILD_TYPE=Dev`, then run the complete CTest
   suite. `tags-filter-bool-differential` compares metadata, tag preservation,
   reference inclusion, ordering, and inversion against the original command.
5. Run address/undefined-behavior sanitizers and the install check where its
   dependencies are available. Repeat performance measurements with identical
   inputs, output formats, and cache conditions.

Do not replace the expression tree with distributive normal-form expansion:
that can grow exponentially. Keep parsing/compilation out of object matching
and avoid per-object allocations. Parser limits and n-ary AND/OR nodes bound
evaluation depth independently of chain length.
