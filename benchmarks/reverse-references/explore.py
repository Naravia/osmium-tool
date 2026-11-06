#!/usr/bin/env python3
"""Compare unchanged getparents with isolated sparse-ID scan prototypes."""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import random
import re
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
    parser.add_argument("--cache-results", type=Path, required=True)
    parser.add_argument("--reference-results", type=Path, required=True)
    parser.add_argument("--prototype", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error("--repeats must be positive")
    args.output_dir.mkdir(parents=True, exist_ok=False)
    cache = json.loads(args.cache_results.read_text())
    reference = json.loads(args.reference_results.read_text())["reference_validation"]
    source = cache["identities"]["input"]
    osmium = cache["identities"]["improved"]
    seeds = {}
    for selector in ("classic", "compound"):
        seeds["direct_" + selector] = next(run["output"] for run in cache["runs"]
                                           if run["label"] == "improved_bool_" + selector and run["round"] == 1)
    seeds["expanded_compound"] = next(run["output"] for run in reference["brandenburg"]
                                       if run["selector"] == "bool" and not run["strip_tags"] and run["version"] == "improved")
    for item in [source, osmium, *seeds.values()]:
        if identity(Path(item["path"]))["sha256"] != item["sha256"]:
            raise RuntimeError("An input differs from its recorded snapshot")
    cases = [(group, strategy) for group in seeds for strategy in
             (("dense", "hash", "sorted") if group.startswith("direct") else ("dense", "hash"))]
    result = {
        "created_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "source": source, "seeds": seeds, "osmium": osmium,
        "prototype": identity(args.prototype),
        "method": {
            "repeats": args.repeats, "seed": 20261106,
            "environment": {"OSMIUM_POOL_THREADS": "1"},
            "scope": "Direct parents only, immutable seed sets, no --add-self, no downward completion",
            "timing": "Wall time including seed reading and PBF writing; excludes output hashing",
            "memory": "Linux os.wait4 per-child ru_maxrss in KiB",
            "cache": "No warmups/cache flush; input hashes read before runs",
            "order": "Shuffled within complete rounds; no concurrent builds or filter processes",
        },
        "runs": [], "summary": {}, "groups": {},
    }
    result_path = args.output_dir / "results.json"
    env = dict(os.environ, OSMIUM_POOL_THREADS="1")

    def save():
        result_path.write_text(json.dumps(result, indent=2) + "\n")

    randomizer = random.Random(20261106)
    expected = {}
    for round_number in range(1, args.repeats + 1):
        order = list(cases)
        randomizer.shuffle(order)
        for group, strategy in order:
            label = group + "_" + strategy
            output = args.output_dir / f"{label}-{round_number}.osm.pbf"
            stdout_path = args.output_dir / f"{label}-{round_number}.stdout"
            stderr_path = args.output_dir / f"{label}-{round_number}.stderr"
            if strategy == "dense":
                command = [osmium["path"], "getparents", "--no-progress", "--verbose",
                           "--generator=reverse-reference-exploration", "-I", seeds[group]["path"],
                           source["path"], "--output", str(output)]
            else:
                command = [str(args.prototype.resolve()), strategy, source["path"], seeds[group]["path"], str(output)]
            print(f"Round {round_number}: {label}", flush=True)
            start = time.perf_counter()
            with stdout_path.open("w") as stdout, stderr_path.open("w") as stderr:
                child = subprocess.Popen(command, env=env, stdout=stdout, stderr=stderr)
                _, status, usage = os.wait4(child.pid, 0)
                child.returncode = os.waitstatus_to_exitcode(status)
            elapsed = time.perf_counter() - start
            stderr_text = stderr_path.read_text()
            run = {"round": round_number, "group": group, "strategy": strategy,
                   "command": command, "return_code": child.returncode,
                   "wall_seconds": elapsed, "stderr": stderr_text}
            result["runs"].append(run)
            save()
            if child.returncode != 0:
                raise RuntimeError(f"Command failed: {label}; see {result_path}")
            run["peak_rss_kib"] = usage.ru_maxrss
            run["output"] = identity(output)
            digest = run["output"]["sha256"]
            if group in expected and digest != expected[group]:
                raise RuntimeError(f"Parent output changed between strategies: {group}")
            expected[group] = digest
            if strategy != "dense":
                parent_counts = re.search(r"Parents: (\d+) ways, (\d+) relations", stderr_text)
                seed_counts = re.search(r"Seeds: (\d+) nodes, (\d+) ways, (\d+) relations", stderr_text)
                result["groups"][group] = {
                    "seeds": dict(zip(("nodes", "ways", "relations"), map(int, seed_counts.groups()))),
                    "parents": dict(zip(("ways", "relations"), map(int, parent_counts.groups()))),
                    "output_sha256": digest, "output_bytes": run["output"]["bytes"],
                }
            save()
            print(f"  {elapsed:.3f} s; {run['peak_rss_kib']/1024:.1f} MiB peak RSS; parity OK", flush=True)
    for group, strategy in cases:
        runs = [run for run in result["runs"] if run["group"] == group and run["strategy"] == strategy]
        result["summary"][group + "_" + strategy] = {
            "median_seconds": statistics.median(run["wall_seconds"] for run in runs),
            "min_seconds": min(run["wall_seconds"] for run in runs),
            "max_seconds": max(run["wall_seconds"] for run in runs),
            "median_peak_rss_mib": statistics.median(run["peak_rss_kib"] for run in runs) / 1024,
        }
    result["all_parent_outputs_identical_per_seed_set"] = True
    result["completed_at"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    save()
    print(json.dumps({"summary": result["summary"], "groups": result["groups"]}, indent=2), flush=True)


if __name__ == "__main__":
    main()
