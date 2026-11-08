# Brandenburg downward relation depth benchmark

Same current executable and workload; only `--downward-relation-hops=1` differs.
Boolean expressions, `--include-parents --parent-hops=2 --parents-expression=r/type=multipolygon --remove-tags`.
`OSMIUM_POOL_THREADS=-2`. Three alternating pairs; medians reported. No cache flush or explicit warmups.
Wall time includes output writing, excludes post-run inspection. Peak memory is per-process Linux maximum RSS.

| Metric | Option absent (recursive) | Downward N=1 |
| --- | ---: | ---: |
| Wall time, median | 43.409 s | 42.520 s |
| Peak RSS, median | 214.816 MiB | 224.227 MiB |
| CPU time, median | 61.618 s | 59.349 s |
| PBF bytes | 14,432,744 | 14,415,828 |
| Nodes | 1,419,799 | 1,417,962 |
| Ways | 60,341 | 60,260 |
| Relations | 4,188 | 4,181 |
| Total objects | 1,484,328 | 1,482,403 |
| Input passes | 7 | 6 |

## Individual runs

| Round | Mode | Wall s | Peak RSS MiB | CPU s |
| --- | --- | ---: | ---: | ---: |
| 1 | recursive | 43.402 | 214.816 | 61.602 |
| 1 | depth1 | 42.629 | 223.781 | 59.417 |
| 2 | depth1 | 42.433 | 224.227 | 59.231 |
| 2 | recursive | 43.457 | 214.758 | 61.728 |
| 3 | recursive | 43.409 | 222.891 | 61.618 |
| 3 | depth1 | 42.520 | 224.434 | 59.349 |

## Comparison

```json
{
  "wall_reduction_percent": 2.0474698279510317,
  "peak_rss_reduction_percent": -4.380557525503237,
  "output_size_reduction_percent": 0.11720570946176512,
  "object_count_reduction_percent": 0.12968831686797078,
  "removed_counts": {
    "nodes": 1837,
    "ways": 81,
    "relations": 7
  },
  "reverse_relationships_byte_identical": true,
  "recursive_matches_pre_feature_saved_output": true,
  "reverse_relationships_match_pre_feature_saved_output": true
}
```

N=1 intentionally omits dependencies below the relation boundary; smaller output is not equivalent completeness.
Three local warm-cache observations are not a general performance guarantee.

Input SHA-256: `c4709c69cbb154db39a4d18d1f96dfb0637377a47053d54c78750393e6d93111`; 296,211,267 bytes.
Expression SHA-256: `eec4c4d01d699929b61f4063036b0960ed668d3e4522eece74766229cf605c01`.
Executable SHA-256: `7889c8ae07ca2d16ae0dc68ba5ffd352567664f50e0d6b678060a732654f44dc`.
Full commands, timings, hashes, host details and identities: `results.json`.
