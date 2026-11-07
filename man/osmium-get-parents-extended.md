# NAME

osmium-get-parents-extended - find direct parents using sparse seed IDs

# SYNOPSIS

**osmium get-parents-extended** [*OPTIONS*] *OSM-FILE* [*ID*...]\
**osmium get-parents-extended** [*OPTIONS*] *OSM-FILE* **-I** *SEED-OSM-FILE*

# DESCRIPTION

Find objects in the source that directly reference the specified seed IDs.
Node seeds can have way and relation parents. Way and relation seeds can have
relation parents. IDs are typed: n1, w1, and r1 are distinct identities.

Only **one reverse hop** is followed. The seed sets are frozen before lookup;
new parents and their dependencies never become additional seeds. This finds
explicit membership, not spatial containment. Only parents available in the
supplied source can be found, not parents outside a regional extract.

Seed IDs may be positional, read from text files, or read from OSM files.
With **-I**, only seed identities are used; parent records and optional self
records always come from the source. Duplicate seed IDs are ignored. An
explicitly supplied empty seed file produces an empty result.

Sparse hash sets store the IDs without allocating bitmaps over their numeric
range. The source is scanned once. Source nodes are skipped unless **-s**
requires them; source ways are skipped when node seeds or qualifying way
parents are absent. These are entity masks, not PBF random-access indexes.

The source must be a positive-ID snapshot, not a history/change file.
Source STDIN is supported if seed IDs are available separately, with **-F**
when required. Seed OSM files must be named files. Text IDs may come from
STDIN only when the source is a named file.

Output objects retain their original metadata, tags, and member/node lists,
in source order, once per source object. **Downward dependencies are not
automatically included.** Use **tags-filter-bool --include-parents** for an
integrated selection with normal downward reference completion. A missing
seed or parent in the source is not fetched externally.

# OPTIONS

-i FILE, --id-file=FILE
: Read one typed ID per line. Leading whitespace, trailing comments beginning
  with #, and text after the first whitespace-separated ID are ignored. May
  be repeated. Use - for text STDIN.

-I FILE, --id-osm-file=FILE
: Read seed identities from an OSM snapshot file. May be repeated.

--default-type=TYPE
: Type of positional/text IDs without a prefix: node (default), way, or relation.

-s, --add-self
: Also include available seed objects from the source. Seed inclusion is not
  restricted by parent predicates. Parent relationships are still recorded
  when a seed is also a parent.

--parents-expression=EXPRESSION
: Apply a Boolean tag predicate to candidate parents. Supports the same
  grammar, atomic semantics, type prefixes, and area eligibility as
  **tags-filter-bool**. Repeated options are ORed. This only restricts parents,
  not seeds. A node-only parent scope selects no parents.

--parent-relationships=FILE
: Write a TSV sidecar with columns child, parent, role_percent_escaped, and
  member_index. IDs have n/w/r prefixes; indices are zero-based positions in
  the parent's original node/member list. Way edges have an empty role.
  Percent, tab, carriage return, and newline in roles are percent-escaped.
  Each matching occurrence is recorded, including repeated closing nodes and
  multiple roles. This is not a deduplicated child/parent-pair list. Only
  original-seed relationships are recorded, not downward dependencies.
  Requires a named file distinct from inputs and OSM output. Existing files
  require **--overwrite**. **--fsync** applies to OSM output, not the TSV.

@MAN_COMMON_OPTIONS@
@MAN_PROGRESS_OPTIONS@
@MAN_INPUT_OPTIONS@
@MAN_OUTPUT_OPTIONS@

# EXAMPLES

Find multipolygon parents of previously selected roots:

    osmium get-parents-extended source.osm.pbf -I roots.osm.pbf \
      --parents-expression='r/type=multipolygon' -o parents.osm.pbf

Include seed objects and write a child-to-parent mapping:

    osmium get-parents-extended source.osm.pbf n1 w11 r24 -s \
      --parent-relationships=edges.tsv -o context.osm.pbf

# DIAGNOSTICS

Exit codes are 0 on success, 1 on a data/output error (including unsupported
source IDs/history discovered while reading), and 2 on invalid arguments or
seed inputs rejected during setup. Successful lookup does not assert
reference completeness of the source or output, or that all seeds were found.

# SEE ALSO

**osmium-tags-filter-bool**(1), **osmium-getparents**(1), **osmium-getid**(1)
