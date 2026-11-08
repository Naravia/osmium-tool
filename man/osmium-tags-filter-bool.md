
# NAME

osmium-tags-filter-bool - filter objects using Boolean tag expressions


# SYNOPSIS

**osmium tags-filter-bool** \[*OPTIONS*\] *OSM-FILE* *EXPRESSION*...\
**osmium tags-filter-bool** \[*OPTIONS*\] \--expressions=*FILE* *OSM-FILE*


# DESCRIPTION

Get objects matching Boolean expressions from the input and write them to the
output. Expressions support AND, OR, NOT, and parentheses. Each tag predicate
is matched against the complete tag list of one object, so an AND can require
different tags on that object. Tags on referenced objects do not participate
in matching.

Separate command-line expressions and expression-file lines are ORed. Put
conditions that must all hold inside one expression. Command-line expressions
are evaluated first, followed by file expressions, in their given order.

Referenced nodes of matching ways and members of matching relations are also
included, recursively by default. **--downward-relation-hops** can bound relation
expansion; **\--omit-referenced/-R** disables all downward completion. Referenced
objects need not match the expression. Output order is the input order.

Without **--include-parents**, **-R** reads the input once. Otherwise multiple passes may be needed
to complete references. STDIN is supported only with **-R**. History files
work correctly only with **-R**. Do not use this command on change files.

This is a separate command. It does not change the syntax or behavior of
**osmium tags-filter**.


# OPTIONS

-e FILE, \--expressions=FILE
:   Read one complete Boolean expression per line. Lines are ORed. Blank and
    whitespace-only lines are ignored. A # starts a comment outside quotes
    unless escaped with a backslash. LF and CRLF line endings are supported.
    No multiline expressions are supported. An empty file contributes no
    matches. Command-line expressions can also be supplied.

-i, \--invert-match
:   Invert the combined matching result, including object-type scopes. Objects
    of types not selected by any expression therefore also match the inversion.
    Reference completion is applied after inversion and may include otherwise
    excluded objects.

-R, \--omit-referenced
:   Do not add downward dependencies. Parent objects are still included when
    **--include-parents** is used. STDIN is permitted only without parent lookup.
    Cannot be combined with **--downward-relation-hops**, including a limit of 0.

\--downward-relation-hops=N
:   Follow at most N downward relation-member edges from every intentionally
    selected relation: original matches (after inversion) and explicitly
    discovered reverse parents. Non-negative integers only. With this option
    absent, completion remains recursive and unchanged.

    At 0, retain selected relation headers and full member lists without following
    their members. At 1, retain their direct node, way, and relation members;
    member relations retain their headers and member lists but do not introduce
    further dependencies unless independently selected. Higher limits permit
    further relation-member expansion. Relation-to-way consumes one hop;
    completing an included way's available nodes consumes no additional hop.
    Independently selected ways retain their available nodes even at limit 0.

    Every selected relation starts with the full budget, even if also encountered
    as a member. Overlapping paths use the shallowest reachable depth (largest
    remaining budget); cycles terminate without repeatedly expanding relations.
    Results do not depend on the order of relations in the source.

    Independent of **--include-parents** and **--parent-hops**. Dependencies never
    become reverse-lookup seeds, and **--parents-expression** does not restrict
    them. Selected objects keep their tags under **--remove-tags**; dependency-only
    objects lose tags. Cannot be combined with **-R**, even at 0. Requires a
    replayable source file, not STDIN, like normal reference completion.

-t, \--remove-tags
:   Remove tags from referenced-only objects. Objects that independently match
    the selection (after inversion, if enabled) retain their tags, even when
    also referenced. Explicitly included parents retain their tags too. Only
    dependency-only objects lose tags. This option has no effect with **-R**.

\--include-parents
:   Include objects referencing the original matches (after inversion), with
    one reverse hop by default. **--parent-hops** selects a different maximum
    reverse depth. Each breadth-first level is frozen and visited objects are
    not expanded again. Geometry-support objects never become reverse seeds.
    Normal downward reference completion then applies to matches and parents,
    unless **-R** is used.
    This may considerably expand output, especially for route/boundary parents.
    Requires a replayable positive-ID snapshot file, even with **-R**; history,
    change files, and source STDIN are unsupported in this mode. Uses the same
    in-process engine as **get-parents-extended**, not a subprocess. Existing
    behavior remains unchanged without this option.

\--parent-hops=N
:   Maximum reverse membership depth from the original matches (default: 1).
    Zero disables parent lookup; 2 includes parents and grandparents. Requires
    **--include-parents**. Non-negative integers only. This controls reverse
    depth, **not** input passes or downward dependency depth. Normal downward
    completion remains recursive unless bounded by **--downward-relation-hops**
    or disabled by **-R**. Every intentionally
    selected ancestor retains its tags under **--remove-tags**.

\--parents-expression=EXPRESSION
:   Restrict qualifying parents using Boolean tag expressions, for example
    `r/type=multipolygon`. May be repeated; alternatives are ORed. Does not
    restrict original matches or downward dependencies. The predicate also
    controls traversal at every reverse hop: nonmatching intermediate parents
    are not traversed. Requires
    **--include-parents**.

\--parent-relationships=FILE
:   Write traversed reverse membership occurrences as TSV. Columns are
    child, parent, role_percent_escaped, member_index; IDs are typed, indices
    are zero-based, and roles escape percent/tab/CR/LF. Repeated occurrences
    and roles are retained. At depth greater than 1 children may be parents
    from earlier levels; rows are ordered by hop then source order. Cyclic
    edges to previously visited objects are recorded without re-expansion.
    Depth 0 writes only the header. Downward dependency edges are not recorded.
    Requires **--include-parents**, a named path distinct from inputs/OSM
    output, and **--overwrite** to replace an existing file. **--fsync** applies
    only to OSM output. See **osmium-get-parents-extended**(1).

@MAN_COMMON_OPTIONS@
@MAN_PROGRESS_OPTIONS@
@MAN_INPUT_OPTIONS@
@MAN_OUTPUT_OPTIONS@


# BOOLEAN EXPRESSIONS

The grammar, in order of increasing precedence, is:

    expression := or-expression
    or-expression := and-expression { OR and-expression }
    and-expression := unary-expression { AND unary-expression }
    unary-expression := NOT unary-expression
                      | '(' expression ')'
                      | tag-predicate

NOT binds more tightly than AND, which binds more tightly than OR. For example,
`a OR b AND c` means `a OR (b AND c)`. Evaluation short-circuits from left to
right. There is no implicit AND and no symbolic operator aliases.

Operators are uppercase, standalone words outside quotes. Separate them from
tag text with whitespace or parentheses. Lowercase words are ordinary tag
text, and `name=AND` is a predicate, not an operator. Spaces inside predicates
are retained; `name=New York` is one predicate, not two conditions. For clarity,
quote values containing spaces or operator words.

## Object types

An optional leading prefix scopes the entire expression: **n/** for nodes,
**w/** for ways, **r/** for relations, and **a/** for areas. Combinations such
as **nw/** or **wa/** are allowed. Without a prefix, or with only **/**, all
nodes, ways, and relations are eligible. For example:

    nw/(amenity=restaurant AND (name!= OR brand!=))
    a/(building AND NOT building=construction)

The scope is outside the Boolean expression: `w/NOT building` selects ways
without that key, not every node and relation. There are no per-predicate type
scopes; use separate expressions for alternatives with different scopes.

As with **tags-filter**, areas are closed ways with at least four node
references and relations tagged `type=multipolygon` or `type=boundary`.
No geometry validity check or area assembly is performed.

## Tag predicates

Predicates use the same atomic matching rules as **tags-filter**:

`key`
:   Key exists, with any value, including an empty value.

`key=value`
:   Key exists with the specified value.

`key!=value`
:   Key exists with a value different from the specified value. An absent key
    does not match. `NOT key=value`, in contrast, also matches an absent key.

`key=`
:   Key exists with an empty value.

`key!=`
:   Key exists with a non-empty value. Whitespace-only values are non-empty.

`NOT key`
:   Key is absent. This can match an object with no tags at all.

Comma-separated keys or values express alternatives within a predicate, for
example `name,name:de!=` or `highway=primary,secondary`. A trailing asterisk
matches a prefix, as in `addr:*`. A leading asterisk matches a substring, as
in `name=*Paris`. Do not combine comma lists and wildcard expressions.
Matching is case-sensitive. Leading/trailing spaces in atomic keys and values
are stripped by the existing matcher utilities.

## Quoting and escaping

Single or double quotes protect portions of a predicate from Boolean
tokenization. A backslash quotes the next character, inside or outside quotes.
For example:

    name="Rock AND Roll" AND building
    name='Cafe (West)' AND amenity=cafe
    "AND"
    name=Cafe\(West\)

The third example matches a key named `AND`. Quote a slash-containing key at
the start of an expression to distinguish it from an object-type prefix.
Slashes in values such as `website=https://example.com` need no escaping.

Quotes and backslashes are removed before constructing the atomic matcher.
They protect Boolean delimiters, not the atomic metacharacters `=`, `*`, and
`,`; for example `name="a,b"` still matches either value, and `name="x*"`
still matches a prefix. Literal matching of those metacharacters is not added
by this command.

The shell also processes quotes and backslashes. Enclose a whole expression
in shell quotes, usually single quotes, when invoking the command:

    osmium tags-filter-bool input.osm.pbf 'name="Rock AND Roll" AND building'

In an expression file, quoted or escaped # characters are literal:

    n/name="Rock AND Roll (#1)" # this is a comment
    n/name=\#literal


# DIAGNOSTICS

Exit codes are 0 on success, 1 on a data-processing error, and 2 on an argument
or expression error. Invalid expressions are rejected before opening the
output. Diagnostics include a 1-based byte column, and for expression files
also the filename and line number.

Each expression is limited to 65536 bytes, 4096 expression-tree nodes, and 128
levels of nested parentheses/NOT. Long AND/OR chains do not increase nesting.


# BOUNDED MATERIALIZATION

The downward limit controls object retention, not interpretation or geometry
assembly. Retained objects keep their original metadata, member order, roles,
and repeated memberships. Relation member lists are never trimmed to hide
references outside the bound. For example, for `r1 -> r2 -> w3 -> nodes`, limit 1
retains r1 and r2 but not w3 or its nodes. Independently selecting r2 gives it
its own full budget, restoring w3 and its available nodes at limit 1.

Bounded output can intentionally fail **osmium check-refs -r** because retained
relation headers still reference omitted objects. Missing source members and
way nodes are not fetched. The option does not imply reference completeness,
repair geometry, or infer inherited tags. Distinguishing an object omitted by
policy from one absent in the source still requires separate source-presence
evidence; this feature does not provide that evidence. The parent relationship
TSV continues to describe only reverse traversal occurrences, not downward
dependencies or selection provenance.


# MEMORY AND PERFORMANCE

Expressions are compiled once. Evaluation does not allocate per object and
does not expand expressions into potentially exponential OR-of-AND groups.
Worst-case matching work is proportional to the evaluated predicates times
the number of tags on an object. Put inexpensive, likely-to-fail conditions
early in AND expressions and likely-to-match alternatives early in OR
expressions.

Results for empty tag lists are precomputed, including logical NOT. Filters
consisting entirely of ORed atomic predicates use a single traversal of the
tag list; general Boolean expressions retain short-circuit evaluation.
Identical decoded predicates share matchers. In general Boolean expressions,
each evaluated predicate scans an object's tags at most once per filter match;
later visits reuse its cached result without evaluating skipped branches.

Reference completion retains object-ID tables in memory. Without parent lookup,
**-R** streams matching over the input without retaining these IDs. Compiled
expressions and normal I/O buffers are still kept in memory.

The Boolean extension's matching and dependency bitmaps allocate 1 KiB chunks
(8,192 numeric IDs per chunk), rather than libosmium's default 4 MiB chunks.
This reduces wasted bitmap storage for sparse regional selections. Memory still
depends on the ID distribution, pointer-directory size, and I/O buffers; it is
not a fixed per-object bound. This tuning does not change original commands or
libosmium defaults, and does not reduce the number of input passes.

With **--include-parents**, original matches and discovered parents are retained
as sparse typed ID sets. For a maximum depth N, parent discovery needs up to N
source-reader passes, stopping at exhaustion. Seed collection and ordered output
add two passes; unbounded downward completion can add up to three more. Thus **-R**
uses up to N+2 passes, or normal completion up to N+5 (three and six at the
default depth of 1). A hop is a membership edge, not an input pass.

With **--downward-relation-hops=D**, bounded dependency discovery uses at most
max(1, D) relation-reader passes when relation selection is needed: one to find
all selected relations (and their direct members for D > 0), followed by frozen
breadth-first levels. It stops when the frontier is exhausted or the bound is
reached. Way-node completion adds at most one way-reader pass, and ordered output
adds one pass. Thus without parents the maximum is max(1, D)+2 passes; with
parent depth N it is N+max(1, D)+3, including seed collection. Unneeded type
passes are skipped. Reader passes scan the relevant source types even for a
small frontier, so large depth limits can increase I/O compared to unbounded
completion.

Bounded mode retains selection/dependency ID bitmaps and sparse current/next
relation frontiers. It enforces the bound during discovery, without allocating
an all-relations membership graph or building a whole recursive closure and
pruning it afterward. Frontier memory is proportional to reached relation IDs,
in addition to bitmap and I/O storage described above.


# EXAMPLES

Require two values and at least one additional non-empty tag:

    osmium tags-filter-bool input.osm.pbf \
        'key1=value1 AND key2=value2 AND (key3!= OR key4!=)' \
        -o filtered.osm.pbf

Find buildings without a non-empty address, omitting referenced objects:

    osmium tags-filter-bool -R input.osm.pbf \
        'a/(building AND NOT addr:housenumber!=)' -o buildings.osm.pbf

Include up to two reverse hops and then complete downward dependencies:

    osmium tags-filter-bool input.osm.pbf 'w/building' \
        --include-parents --parent-hops=2 -o context.osm.pbf

Include one reverse parent hop, then only direct relation members and way nodes:

    osmium tags-filter-bool input.osm.pbf 'w/building' \
        --include-parents --parent-hops=1 --downward-relation-hops=1 \
        -o bounded-context.osm.pbf

When generating candidates for downstream rules, preserve every valid
alternative. A candidate filter must be a safe superset of downstream
acceptance rules; ANDing alternative contracts can incorrectly drop objects.


# SEE ALSO

* [**osmium**(1)](osmium.html), [**osmium-tags-filter**(1)](osmium-tags-filter.html), [**osmium-file-formats**(5)](osmium-file-formats.html)
