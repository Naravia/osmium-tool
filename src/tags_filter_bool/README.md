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

## Updating upstream

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
