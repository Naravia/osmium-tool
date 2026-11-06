
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
included, recursively, unless **\--omit-referenced/-R** is used. Referenced
objects need not match the expression. Output order is the input order.

With **-R** the input is read once. Otherwise up to three passes may be needed
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
:   Do not add referenced objects. This permits reading OSM input from STDIN.

-t, \--remove-tags
:   Remove tags from referenced-only objects. Objects that independently match
    the selection (after inversion, if enabled) retain their tags, even when
    also referenced. This option has no effect with **-R**.

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

Reference completion retains object-ID tables in memory. With **-R**, matching
streams over the input without retaining these IDs. Compiled expressions and
normal I/O buffers are still kept in memory.


# EXAMPLES

Require two values and at least one additional non-empty tag:

    osmium tags-filter-bool input.osm.pbf \
        'key1=value1 AND key2=value2 AND (key3!= OR key4!=)' \
        -o filtered.osm.pbf

Find buildings without a non-empty address, omitting referenced objects:

    osmium tags-filter-bool -R input.osm.pbf \
        'a/(building AND NOT addr:housenumber!=)' -o buildings.osm.pbf

When generating candidates for downstream rules, preserve every valid
alternative. A candidate filter must be a safe superset of downstream
acceptance rules; ANDing alternative contracts can incorrectly drop objects.


# SEE ALSO

* [**osmium**(1)](osmium.html), [**osmium-tags-filter**(1)](osmium-tags-filter.html), [**osmium-file-formats**(5)](osmium-file-formats.html)
