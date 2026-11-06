#!/usr/bin/env python3
"""Check whether unresolved output references were already absent in input."""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def identity(path):
    path = path.resolve()
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"path": str(path), "sha256": digest, "bytes": path.stat().st_size}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--validation-results", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    validation = json.loads(args.validation_results.read_text())
    if "completed_at" not in validation:
        raise RuntimeError("Run the complete reference validation first")
    env = dict(os.environ, OSMIUM_POOL_THREADS="1")
    binary = validation["identities"]["improved"]
    source = validation["identities"]["input"]
    for item in (binary, source):
        if identity(Path(item["path"]))["sha256"] != item["sha256"]:
            raise RuntimeError("Validation input or executable changed")
    result = {
        "created_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "validation_results": identity(args.validation_results),
        "method": "Compare missing child-object IDs from check-refs -r -i against the source's missing IDs",
        "outputs": [],
    }

    def missing(path, label):
        output = args.output_dir / (label + "-missing.txt")
        argv = [binary["path"], "check-refs", "-r", "-i", "--no-progress", str(path)]
        with output.open("w") as stream:
            completed = subprocess.run(argv, env=env, stdout=stream, stderr=subprocess.PIPE, text=True)
        if completed.returncode not in (0, 1):
            raise RuntimeError(f"Reference checker failed: {completed.stderr}")
        counts = re.findall(r"^.*\s+missing: (\d+)$", completed.stderr, re.MULTILINE)
        if len(counts) != 4 or completed.returncode != int(any(int(count) for count in counts)):
            raise RuntimeError(f"Incomplete reference-check diagnostics: {completed.stderr}")
        absent = set()
        for line in output.read_text().splitlines():
            match = re.fullmatch(r"([nwr]-?\d+) in [wr]-?\d+", line)
            if not match:
                raise RuntimeError(f"Unexpected reference diagnostic: {line}")
            absent.add(match.group(1))
        return absent, {"command": argv, "return_code": completed.returncode,
                        "stderr": completed.stderr, "missing_references_file": identity(output),
                        "unique_missing_child_ids": len(absent)}

    source_missing, result["source"] = missing(Path(source["path"]), "input")
    for index, run in enumerate(validation["brandenburg"]):
        item = run["output"]
        path = Path(item["path"])
        if identity(path)["sha256"] != item["sha256"]:
            raise RuntimeError("A validated output changed")
        absent, checked = missing(path, f"output-{index}")
        unexpected = sorted(absent - source_missing)
        checked.update({"selector": run["selector"], "strip_tags": run["strip_tags"],
                        "version": run["version"], "output": item,
                        "unexpected_missing_ids": unexpected,
                        "all_missing_ids_already_absent_in_source": not unexpected})
        result["outputs"].append(checked)
        print(run["selector"], run["strip_tags"], run["version"],
              f"{len(absent)} unique missing IDs; {len(unexpected)} not explained by source", flush=True)
    result["all_available_references_included"] = all(
        run["all_missing_ids_already_absent_in_source"] for run in result["outputs"])
    result["completed_at"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (args.output_dir / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    if not result["all_available_references_included"]:
        raise RuntimeError("Output omitted an input-available reference; see results.json")


if __name__ == "__main__":
    main()
