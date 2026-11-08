#!/usr/bin/env python3
"""Check one-hop compatibility and two-hop parity with unchanged original commands."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time


def identity(path):
    path = Path(path).resolve()
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"path": str(path), "sha256": digest, "bytes": path.stat().st_size}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--osmium", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True,
                        help="Existing command-validation-results.json")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    previous = json.loads(args.manifest.read_text())
    for key in ("source", "seeds", "baseline", "expressions"):
        item = previous[key]
        if identity(item["path"])["sha256"] != item["sha256"]:
            raise RuntimeError(f"Retained {key} changed")
    binary = str(args.osmium.resolve())
    baseline = previous["baseline"]["path"]
    source = previous["source"]["path"]
    seeds = previous["seeds"]["path"]
    expressions = previous["expressions"]["path"]
    env = dict(os.environ, OSMIUM_POOL_THREADS="-2")
    common = ["--no-progress", "--generator=reverse-reference-exploration"]
    predicate = "r/type=multipolygon"
    result = {key: previous[key] for key in ("source", "seeds", "baseline", "expressions")}
    result.update(binary=identity(binary), OSMIUM_POOL_THREADS="-2", steps=[], comparisons=[],
                  note="Focused correctness checks, not repeated performance measurements; "
                       "original getid -r may exit 1 for missing regional members")
    result_path = args.output_dir / "results.json"

    def save():
        result_path.write_text(json.dumps(result, indent=2) + "\n")

    def run(label, command, allowed=(0,)):
        command = [str(arg) for arg in command]
        print(label, flush=True)
        start = time.perf_counter()
        completed = subprocess.run(command, env=env, capture_output=True, text=True)
        result["steps"].append({"label": label, "command": command,
                                "seconds": time.perf_counter() - start,
                                "return_code": completed.returncode,
                                "stdout": completed.stdout, "stderr": completed.stderr})
        save()
        if completed.returncode not in allowed:
            raise RuntimeError(f"Command failed: {label}; see {result_path}")
        return completed.stderr

    def same(label, actual, expected):
        actual, expected = identity(actual), identity(expected)
        equal = actual["sha256"] == expected["sha256"]
        result["comparisons"].append({"label": label, "actual": actual,
                                      "expected": expected, "byte_identical": equal})
        save()
        if not equal:
            raise RuntimeError(f"Parity failed: {label}")

    # Compose each level using only the unchanged original commands. Filtering
    # at each level independently verifies pruning, not just final filtering.
    parents = []
    for hop in (1, 2):
        unfiltered = args.output_dir / f"original-hop-{hop}-all.osm.pbf"
        filtered = args.output_dir / f"original-hop-{hop}-multipolygon.osm.pbf"
        run(f"original parent hop {hop}", [baseline, "getparents", source,
                                          "-I", seeds if hop == 1 else parents[-1],
                                          *common, "-o", unfiltered])
        run(f"original parent filter {hop}", [baseline, "tags-filter", unfiltered,
                                             predicate, "-R", *common, "-o", filtered])
        parents.append(filtered)

    union = args.output_dir / "two-hop-parent-union.osm.pbf"
    run("original two-hop parent union", [baseline, "getid", source, "-I", parents[0],
                                           "-I", parents[1], *common, "-o", union])
    actual = args.output_dir / "two-hop-parents.osm.pbf"
    run("standalone two-hop lookup", [binary, "get-parents-extended", source, "-I", seeds,
                                      "--parent-hops=2", "--parents-expression=" + predicate,
                                      *common, "-o", actual])
    same("two-hop standalone versus original composition", actual, union)

    for depth in (1, 2):
        actual = args.output_dir / f"integrated-{depth}.osm.pbf"
        edges = args.output_dir / f"integrated-{depth}.tsv"
        # Omit the option for depth 1 to check backward-compatible default output.
        flags = [] if depth == 1 else ["--parent-hops=2"]
        stderr = run(f"integrated depth {depth}", [binary, "tags-filter-bool", source,
                      "-e", expressions, "--include-parents", "--parents-expression=" + predicate,
                      "--parent-relationships=" + str(edges), "--remove-tags", "--verbose",
                      *flags, *common, "-o", actual])
        passes = re.search(r"Needed (\d+) pass\(es\)", stderr)
        result[f"integrated_{depth}_source_passes"] = int(passes.group(1)) if passes else None
        if depth == 1:
            reference = next(item for item in previous["comparisons"] if item["mode"] == "strip")
            same("default one-hop PBF versus validated baseline", actual, reference["actual"]["path"])
            step = next(item for item in previous["steps"] if item["label"] == "standalone filtered parents")
            reference_edges = next(arg.split("=", 1)[1] for arg in step["command"]
                                   if arg.startswith("--parent-relationships="))
            same("default one-hop TSV versus validated baseline", edges, reference_edges)
        else:
            expected = args.output_dir / "two-hop-composed-strip.osm.pbf"
            run("original two-hop downward completion", [baseline, "getid", source, "-I", seeds,
                 "-I", parents[0], "-I", parents[1], "-r", "-t", *common, "-o", expected], allowed=(0, 1))
            same("two-hop integrated versus original composition", actual, expected)
    result["all_checks_passed"] = True
    save()
    print("All default compatibility and two-hop composition checks passed.", flush=True)


if __name__ == "__main__":
    main()
