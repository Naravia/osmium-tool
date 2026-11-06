#!/usr/bin/env python3
"""Compare baseline and improved osmium binaries on identical PBF selectors."""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess
import time


def identity(path):
    path = path.resolve()
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"path": str(path), "sha256": digest, "bytes": path.stat().st_size}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--improved", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True,
                        help="Original benchmark-results.json, including expected output hashes")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--classic", type=Path, required=True)
    parser.add_argument("--boolean", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error("--repeats must be positive")
    # osmium interprets suffixes in argv[0] as subcommands.
    if args.baseline.name != "osmium" or args.improved.name != "osmium":
        parser.error("Both binaries must be named osmium; place them in separate directories")
    args.output_dir.mkdir(parents=True, exist_ok=False)
    reference = json.loads(args.reference.read_text())
    identities = {name: identity(path) for name, path in (
        ("input", args.input), ("classic", args.classic), ("bool", args.boolean),
        ("baseline", args.baseline), ("improved", args.improved))}
    expected_inputs = {"input": reference["input"]["sha256"],
                       "classic": reference["filters"]["classic"]["sha256"],
                       "bool": reference["filters"]["bool"]["sha256"]}
    for name, expected in expected_inputs.items():
        if identities[name]["sha256"] != expected:
            raise RuntimeError(f"Original benchmark input identity mismatch: {name}")

    expected_outputs = {"classic": reference["runs"][0]["output"],
                        "bool": reference["runs"][2]["output"]}
    cases = [
        ("baseline_classic", "baseline", "tags-filter", "classic"),
        ("baseline_bool_classic", "baseline", "tags-filter-bool", "classic"),
        ("baseline_bool_compound", "baseline", "tags-filter-bool", "bool"),
        ("improved_bool_classic", "improved", "tags-filter-bool", "classic"),
        ("improved_bool_compound", "improved", "tags-filter-bool", "bool"),
    ]
    env = dict(os.environ, OSMIUM_POOL_THREADS="1")
    result = {
        "created_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "identities": identities,
        "platform": {"system": platform.platform(), "machine": platform.machine()},
        "method": {
            "repeats": args.repeats, "seed": 20261106,
            "warmups": 0, "cache": "No cache flush; input SHA-256 read before runs",
            "order": "Deterministically shuffled within each complete round",
            "environment": {"OSMIUM_POOL_THREADS": "1"},
            "reference_mode": "-R/--omit-referenced", "output_format": "PBF",
            "timing": "time.perf_counter wall time including PBF output and process exit; output hashing excluded",
            "concurrency": "One filter process at a time",
        },
        "versions": {}, "runs": [], "summary": {},
    }
    for name in ("baseline", "improved"):
        result["versions"][name] = subprocess.check_output(
            [identities[name]["path"], "--version"], env=env, text=True)
    result_path = args.output_dir / "results.json"

    def save():
        result_path.write_text(json.dumps(result, indent=2) + "\n")

    save()
    randomizer = random.Random(20261106)
    for round_number in range(1, args.repeats + 1):
        order = list(cases)
        randomizer.shuffle(order)
        for label, binary, command, selector in order:
            output = args.output_dir / f"{label}-{round_number}.osm.pbf"
            argv = [identities[binary]["path"], command, "--no-progress",
                    "--omit-referenced", "--expressions=" + identities[selector]["path"],
                    identities["input"]["path"], "--output-format=pbf", "--output", str(output.resolve())]
            print(f"Round {round_number}: {label}", flush=True)
            start = time.perf_counter()
            completed = subprocess.run(argv, env=env, stdout=subprocess.DEVNULL,
                                       stderr=subprocess.PIPE, text=True)
            elapsed = time.perf_counter() - start
            run = {"round": round_number, "label": label, "command": argv,
                   "wall_seconds": elapsed, "return_code": completed.returncode,
                   "stderr": completed.stderr}
            if completed.returncode == 0:
                run["output"] = identity(output)
                run["matches_original_output"] = (
                    run["output"]["sha256"] == expected_outputs[selector]["sha256"] and
                    run["output"]["bytes"] == expected_outputs[selector]["size_bytes"])
            result["runs"].append(run)
            save()
            if completed.returncode != 0 or not run.get("matches_original_output"):
                raise RuntimeError(f"Benchmark failed or output changed: {label}; see {result_path}")
            print(f"  {elapsed:.3f} s; original PBF SHA-256 unchanged", flush=True)

    for label, _, _, _ in cases:
        times = [run["wall_seconds"] for run in result["runs"] if run["label"] == label]
        result["summary"][label] = {"seconds": times, "median_seconds": statistics.median(times),
                                     "min_seconds": min(times), "max_seconds": max(times)}
    result["all_outputs_match_original"] = all(run["matches_original_output"] for run in result["runs"])
    result["completed_at"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    save()
    print(json.dumps(result["summary"], indent=2), flush=True)


if __name__ == "__main__":
    main()
