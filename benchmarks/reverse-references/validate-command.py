#!/usr/bin/env python3
"""Validate the new command and integrated option against retained Brandenburg oracles."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time


def identity(path):
    path = path.resolve()
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"path": str(path), "sha256": digest, "bytes": path.stat().st_size}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--osmium", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    previous = json.loads(args.manifest.read_text())
    source, seeds, baseline = previous["source"], previous["seeds"]["direct_compound"], previous["osmium"]
    for item in (source, seeds, baseline):
        if identity(Path(item["path"]))["sha256"] != item["sha256"]:
            raise RuntimeError("Retained validation input changed")
    binary = str(args.osmium.resolve())
    env = dict(os.environ, OSMIUM_POOL_THREADS="1")
    result = {"source": source, "seeds": seeds, "baseline": baseline,
              "binary": identity(args.osmium), "steps": [], "comparisons": [],
              "note": "Single-run correctness validation; getid -r can return 1 for source-missing members"}
    generator = "--generator=reverse-reference-exploration"
    result_path = args.output_dir / "results.json"

    def save():
        result_path.write_text(json.dumps(result, indent=2) + "\n")

    def run(label, argv, allowed=(0,)):
        print(label, flush=True)
        start = time.perf_counter()
        completed = subprocess.run([str(arg) for arg in argv], env=env, capture_output=True, text=True)
        step = {"label": label, "command": [str(arg) for arg in argv],
                "seconds": time.perf_counter() - start, "return_code": completed.returncode,
                "stdout": completed.stdout, "stderr": completed.stderr}
        result["steps"].append(step)
        save()
        if completed.returncode not in allowed:
            raise RuntimeError(f"Validation command failed: {label}; see {result_path}")
        print(f"  {step['seconds']:.3f} s, exit {completed.returncode}", flush=True)
        return step

    parents = args.output_dir / "parents.osm.pbf"
    edges = args.output_dir / "parents.tsv"
    run("standalone parents and edges", [binary, "get-parents-extended", source["path"], "-I", seeds["path"],
                                       "--no-progress", generator, "--parent-relationships=" + str(edges), "-o", parents])
    result["standalone_parity"] = identity(parents)["sha256"] == previous["groups"]["direct_compound"]["output_sha256"]
    result["edge_parity"] = identity(edges)["sha256"] == previous["edge_verification"]["edge_file_sha256"]
    if not result["standalone_parity"] or not result["edge_parity"]:
        raise RuntimeError("Standalone parents or membership edges changed")

    oracle_parents = args.output_dir / "multipolygon-oracle.osm.pbf"
    parent_filter = "r/type=multipolygon"
    run("original classic filter parent predicate oracle", [baseline["path"], "tags-filter", parents,
                                                          parent_filter, "-R", "--no-progress", generator, "-o", oracle_parents])
    filtered_parents = args.output_dir / "multipolygon-parents.osm.pbf"
    filtered_edges = args.output_dir / "multipolygon-parents.tsv"
    run("standalone filtered parents", [binary, "get-parents-extended", source["path"], "-I", seeds["path"],
                                        "--parents-expression=" + parent_filter, "--no-progress", generator,
                                        "--parent-relationships=" + str(filtered_edges), "-o", filtered_parents])
    result["parent_predicate_parity"] = identity(oracle_parents)["sha256"] == identity(filtered_parents)["sha256"]
    if not result["parent_predicate_parity"]:
        raise RuntimeError("Parent predicate differs from original classic filtering")

    expressions = Path(__file__).resolve().parents[1] / "tags-filter-bool/predicate-cache-results.json"
    expression_identity = json.loads(expressions.read_text())["identities"]["bool"]
    if identity(Path(expression_identity["path"]))["sha256"] != expression_identity["sha256"]:
        raise RuntimeError("Compound expression file changed")
    result["expressions"] = expression_identity
    for mode in ("omit", "normal", "strip"):
        expected = args.output_dir / f"{mode}-composed.osm.pbf"
        actual = args.output_dir / f"{mode}-integrated.osm.pbf"
        actual_edges = args.output_dir / f"{mode}-integrated.tsv"
        down = [] if mode == "omit" else ["-r"]
        if mode == "strip":
            down.append("-t")
        run(mode + " original getid composition", [baseline["path"], "getid", source["path"],
                                                  "-I", seeds["path"], "-I", oracle_parents, *down,
                                                  "--no-progress", generator, "-o", expected], allowed=(0, 1))
        flags = ["-R"] if mode == "omit" else (["-t"] if mode == "strip" else [])
        step = run(mode + " integrated parent selection", [binary, "tags-filter-bool", source["path"],
                                                           "-e", expression_identity["path"], "--include-parents",
                                                           "--parents-expression=" + parent_filter,
                                                           "--parent-relationships=" + str(actual_edges), *flags,
                                                           "--no-progress", "--verbose", generator, "-o", actual])
        passes = re.search(r"Needed (\d+) pass\(es\)", step["stderr"])
        comparison = {"mode": mode, "expected": identity(expected), "actual": identity(actual),
                      "same_output": identity(expected)["sha256"] == identity(actual)["sha256"],
                      "same_frozen_seed_edges": identity(actual_edges)["sha256"] == identity(filtered_edges)["sha256"],
                      "source_reader_passes": int(passes.group(1)) if passes else None}
        result["comparisons"].append(comparison)
        save()
        if not comparison["same_output"] or not comparison["same_frozen_seed_edges"]:
            raise RuntimeError(f"Integrated output or provenance differs: {mode}")
    result["all_checks_passed"] = True
    save()
    print("All standalone, predicate, integrated-output, and frozen-edge checks passed.", flush=True)


if __name__ == "__main__":
    main()
