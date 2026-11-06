#!/usr/bin/env python3
"""Small direct-parent/edge examples, including forward references and cycles."""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--osmium", type=Path, required=True)
    parser.add_argument("--prototype", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    fixture = Path(__file__).resolve().parents[2] / "test/tags-filter-bool/input.osm.opl"
    env = dict(os.environ, OSMIUM_POOL_THREADS="1")
    cases = [
        ("typed-members", ["n1", "w11", "r24"],
         [("n1", "w10", "", 0), ("n1", "w10", "", 3),
          ("n1", "w13", "", 0), ("n1", "w13", "", 1),
          ("w11", "r20", "outer", 0), ("r24", "r25", "", 0)]),
        ("parent-before-seed-relation", ["r25"], [("r25", "r24", "", 1)]),
        ("one-hop-not-ancestors", ["r20"], [("r20", "r23", "", 0)]),
    ]
    results = {"fixture": str(fixture), "cases": []}

    def invoke(command):
        completed = subprocess.run([str(arg) for arg in command], env=env, capture_output=True, text=True)
        if completed.returncode:
            raise RuntimeError(completed.stderr)
        return {"command": [str(arg) for arg in command], "stderr": completed.stderr}

    for label, ids, expected in cases:
        seeds = args.output_dir / (label + "-seeds.osm.pbf")
        commands = [invoke([args.osmium, "getid", fixture, *ids, "--output", seeds])]
        baseline = args.output_dir / (label + "-getparents.osm.pbf")
        commands.append(invoke([args.osmium, "getparents", fixture, "-I", seeds,
                                "--generator=reverse-reference-exploration", "--output", baseline]))
        record = {"case": label, "seeds": ids, "expected_edges": expected,
                  "parent_sha256": digest(baseline), "commands": commands, "strategies": []}
        for strategy in ("hash", "sorted"):
            output = args.output_dir / f"{label}-{strategy}.osm.pbf"
            edges = args.output_dir / f"{label}-{strategy}.tsv"
            run = invoke([args.prototype, strategy, fixture, seeds, output, edges])
            with edges.open() as stream:
                rows = list(csv.DictReader(stream, delimiter="\t"))
            actual = [(row["child"], row["parent"], row["role_percent_escaped"], int(row["member_index"])) for row in rows]
            run.update({"strategy": strategy, "parent_parity": digest(output) == digest(baseline),
                        "edges_match_expected": actual == expected, "edge_file": str(edges)})
            record["strategies"].append(run)
            if not run["parent_parity"] or not run["edges_match_expected"]:
                raise RuntimeError(f"Example disagrees: {label}, {strategy}")
        results["cases"].append(record)
        print(label, "direct parents and occurrence-level edges agree", flush=True)
    results["all_examples_agree"] = True
    (args.output_dir / "results.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()
