#!/usr/bin/env python3
"""Compare extension-local bitmap tuning with a retained pre-change executable."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def identity(path):
    path = Path(path).resolve()
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"path": str(path), "sha256": digest, "bytes": path.stat().st_size}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True,
                        help="Retained executable; keep its basename 'osmium' for command dispatch")
    parser.add_argument("--osmium", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True,
                        help="Retained parent-hops validation results.json")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--parent-hops", type=int, choices=(1, 2), default=2)
    parser.add_argument("--runs", type=int, default=1,
                        help="Paired rounds; alternates execution order after each round")
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    if args.baseline.name != "osmium" or args.osmium.name != "osmium":
        parser.error("Keep both executable basenames 'osmium' for multicall command dispatch")
    args.output_dir.mkdir(parents=True, exist_ok=False)
    previous = json.loads(args.manifest.read_text())
    for key in ("source", "expressions"):
        if identity(previous[key]["path"])["sha256"] != previous[key]["sha256"]:
            raise RuntimeError(f"Retained {key} changed")
    binaries = {"baseline": identity(args.baseline), "small_chunks": identity(args.osmium)}
    if binaries["baseline"]["sha256"] != previous["binary"]["sha256"]:
        raise RuntimeError("Baseline does not match the pre-change validation executable")
    step = next(item for item in previous["steps"]
                if item["label"] == f"integrated depth {args.parent_hops}")
    reference_output = identity(step["command"][step["command"].index("-o") + 1])
    reference_edges = identity(next(arg.split("=", 1)[1] for arg in step["command"]
                                    if arg.startswith("--parent-relationships=")))
    expected = next(item["actual"] for item in previous["comparisons"]
                    if item["actual"]["path"] == reference_output["path"])
    if reference_output["sha256"] != expected["sha256"]:
        raise RuntimeError("Retained PBF reference changed")
    result = {"binaries": binaries, "source": previous["source"],
              "expressions": previous["expressions"], "OSMIUM_POOL_THREADS": "-2",
              "parent_hops": args.parent_hops, "runs_per_binary": args.runs,
              "reference_output": reference_output, "reference_edges": reference_edges,
              "runs": [],
              "method": "Sequential paired runs, alternating order between rounds; input hashing warms cache; "
                        "no cache flush or invocation warmups. Elapsed time includes PBF/TSV writing and process exit, "
                        "not subsequent hashing. Peak RSS is Linux wait4.ru_maxrss in KiB, not verbose VmPeak."}
    result_path = args.output_dir / "results.json"

    def save():
        result_path.write_text(json.dumps(result, indent=2) + "\n")

    for round_ in range(args.runs):
        order = ("baseline", "small_chunks") if round_ % 2 == 0 else ("small_chunks", "baseline")
        for variant in order:
            label = f"{variant}-{round_ + 1}"
            output = args.output_dir / f"{label}.osm.pbf"
            edges = args.output_dir / f"{label}.tsv"
            command = list(step["command"])
            command[0] = binaries[variant]["path"]
            command[command.index("-o") + 1] = str(output)
            command = [("--parent-relationships=" + str(edges))
                       if arg.startswith("--parent-relationships=") else arg for arg in command]
            print(f"Starting {label}, parent hops {args.parent_hops}", flush=True)
            start = time.perf_counter()
            with (args.output_dir / f"{label}.stderr.log").open("w") as stderr:
                child = subprocess.Popen(command, env=dict(os.environ, OSMIUM_POOL_THREADS="-2"),
                                         stdout=subprocess.DEVNULL, stderr=stderr)
                _, status, usage = os.wait4(child.pid, 0)
                child.returncode = os.waitstatus_to_exitcode(status)
            run = {"variant": variant, "round": round_ + 1, "command": command,
                   "wall_seconds": time.perf_counter() - start, "return_code": child.returncode,
                   "peak_rss_kib": usage.ru_maxrss, "peak_rss_mib": usage.ru_maxrss / 1024,
                   "user_seconds": usage.ru_utime, "system_seconds": usage.ru_stime}
            result["runs"].append(run)
            save()
            if child.returncode:
                raise RuntimeError(f"Command failed: {label}; see stderr log")
            run["output"], run["edges"] = identity(output), identity(edges)
            run["same_reference_output"] = run["output"]["sha256"] == reference_output["sha256"]
            run["same_reference_edges"] = run["edges"]["sha256"] == reference_edges["sha256"]
            save()
            if not run["same_reference_output"] or not run["same_reference_edges"]:
                raise RuntimeError(f"Output or relationship parity failed: {label}")
            print(f"  {run['wall_seconds']:.3f} s, {run['peak_rss_mib']:.1f} MiB RSS; PBF/TSV parity passed", flush=True)
    result["all_checks_passed"] = True
    save()


if __name__ == "__main__":
    main()
