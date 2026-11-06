#!/usr/bin/env python3
"""Check reference-complete output parity before/after predicate caching."""

import argparse
import datetime
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


def objects(path):
    result = {}
    for line in path.read_text().splitlines():
        fields = line.split(" ")
        if fields and re.fullmatch(r"[nwr]-?\d+", fields[0]):
            result[fields[0]] = next(field[1:] for field in fields if field.startswith("T"))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True,
                        help="predicate-cache-results.json from the preceding benchmark")
    parser.add_argument("--output-dir", type=Path, required=True)
    for name in ("previous", "improved", "input", "classic", "bool"):
        parser.add_argument("--" + name, type=Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    args.output_dir.mkdir(parents=True, exist_ok=False)
    identities = {}
    for name in ("previous", "improved", "input", "classic", "bool"):
        path = getattr(args, name) or Path(manifest["identities"][name]["path"])
        identities[name] = identity(path)
        if identities[name]["sha256"] != manifest["identities"][name]["sha256"]:
            raise RuntimeError(f"Identity changed since the benchmark: {name}")
    env = dict(os.environ, OSMIUM_POOL_THREADS="1")
    root = Path(__file__).resolve().parents[2]
    fixture = root / "test/tags-filter-bool/input.osm.opl"
    no_relations = root / "test/tags-filter-bool/input-no-relations.osm.opl"
    result = {
        "created_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "identities": identities,
        "fixture_identities": [identity(fixture), identity(no_relations)],
        "method": {
            "environment": {"OSMIUM_POOL_THREADS": "1"},
            "reference_mode": "References enabled (no -R); normal and --remove-tags",
            "comparison": "Full output SHA-256 and byte size against the pre-cache version",
            "integrity": "check-refs -r on input and every generated output",
            "timings": "Single observations for validation, not repeated performance measurements",
        },
        "fixtures": [], "brandenburg": [], "comparisons": [],
    }
    result_path = args.output_dir / "results.json"

    def save():
        result_path.write_text(json.dumps(result, indent=2) + "\n")

    def invoke(argv):
        start = time.perf_counter()
        completed = subprocess.run(argv, env=env, capture_output=True, text=True)
        return {"command": [str(arg) for arg in argv], "return_code": completed.returncode,
                "wall_seconds": time.perf_counter() - start,
                "stdout": completed.stdout, "stderr": completed.stderr}

    def check_refs(path):
        checked = invoke([identities["improved"]["path"], "check-refs", "-r", "--no-progress", str(path)])
        counts = re.search(r"There are (\d+) nodes, (\d+) ways, and (\d+) relations", checked["stderr"])
        if counts:
            checked["counts"] = dict(zip(("nodes", "ways", "relations"), map(int, counts.groups())))
        checked["missing"] = {" ".join(kind.split()): int(count) for kind, count in
                              re.findall(r"^(Nodes\s+in ways|Nodes\s+in relations|Ways\s+in relations|Relations\s+in relations)\s+missing: (\d+)$",
                                         checked["stderr"], re.MULTILINE)}
        return checked

    compound = "(key1=value1 AND key2=value2 AND key3!=) OR (key1=value1 AND key2=value2 AND key4!=)"
    direct = set("n1 n2 w10 w12 r20 r22".split())
    # OPL permits different spellings of the same escaped tag value. Compare
    # serialized tag fields against canonicalized input, not its source bytes.
    canonical_tags = {}
    result["fixture_normalizations"] = []
    for source in (fixture, no_relations):
        canonical = args.output_dir / ("canonical-" + source.name)
        normalized = invoke([identities["previous"]["path"], "cat", str(source),
                             "--output-format=opl", "--output", str(canonical)])
        result["fixture_normalizations"].append(normalized)
        if normalized["return_code"] != 0:
            save()
            raise RuntimeError("Could not normalize fixture tags")
        canonical_tags[source] = objects(canonical)
    fixture_ids = set(canonical_tags[fixture])
    cases = [
        ("shared-compound", fixture, compound, [],
         "n1 n2 n3 n4 n5 n6 w10 w11 w12 r20 r22", direct),
        ("shared-invert", fixture, compound, ["--invert-match"],
         "n1 n3 n4 n5 n6 n7 n8 n9 w11 w13 w14 r20 r21 r23 r24 r25",
         fixture_ids - direct),
        ("shared-nested-not", fixture, "r/((group AND cycle=yes) OR (group AND NOT cycle))", [],
         "n3 n4 n6 w11 r20 r23 r24 r25", {"r23", "r24"}),
        ("shared-area", fixture, "a/((building AND NOT building=construction) OR (building AND building=yes))", [],
         "n1 n2 n3 n4 n5 n6 w10 w11 w14 r20 r21", {"w10", "w14", "r20", "r21"}),
        ("shared-invert-unscoped", no_relations, "n/((note AND note!=bad) OR (note AND note=bad))", ["--invert-match"],
         "n1 n2 w1", {"n2", "w1"}),
    ]
    for label, input_path, expression, flags, expected, matching in cases:
        original_tags = canonical_tags[input_path]
        for strip in (False, True):
            outputs = []
            for version in ("previous", "improved"):
                output = args.output_dir / f"{label}-{'strip' if strip else 'normal'}-{version}.osm.opl"
                argv = [identities[version]["path"], "tags-filter-bool", "--no-progress", *flags]
                if strip:
                    argv.append("--remove-tags")
                argv.extend([str(input_path), expression, "--output-format=opl", "--output", str(output)])
                run = invoke(argv)
                run.update({"case": label, "strip_tags": strip, "version": version})
                result["fixtures"].append(run)
                if run["return_code"] != 0:
                    save()
                    raise RuntimeError(f"Fixture command failed: {label}")
                actual = objects(output)
                run["expected_ids"] = expected.split()
                run["ids_match"] = list(actual) == expected.split()
                run["tags_match"] = all(tags == (original_tags[obj_id] if not strip or obj_id in matching else "")
                                        for obj_id, tags in actual.items())
                run["output"] = identity(output)
                run["reference_check"] = check_refs(output)
                outputs.append(run["output"])
                save()
                if not run["ids_match"] or not run["tags_match"] or run["reference_check"]["return_code"] != 0:
                    raise RuntimeError(f"Fixture validation failed: {label}; see {result_path}")
            if outputs[0]["sha256"] != outputs[1]["sha256"]:
                raise RuntimeError(f"Fixture output changed: {label}")
    print("20 focused fixture runs passed: IDs, tag retention/removal, full parity, and references.", flush=True)

    print("Checking source Brandenburg reference completeness...", flush=True)
    result["input_reference_check"] = check_refs(identities["input"]["path"])
    save()
    for selector in ("classic", "bool"):
        for strip in (False, True):
            pair = []
            for version in ("previous", "improved"):
                label = f"{selector}-{'strip' if strip else 'normal'}-{version}"
                output = args.output_dir / (label + ".osm.pbf")
                argv = [identities[version]["path"], "tags-filter-bool", "--verbose", "--no-progress"]
                if strip:
                    argv.append("--remove-tags")
                argv.extend(["--expressions=" + identities[selector]["path"], identities["input"]["path"],
                             "--output-format=pbf", "--output", str(output)])
                print(label, flush=True)
                run = invoke(argv)
                run.update({"selector": selector, "strip_tags": strip, "version": version})
                result["brandenburg"].append(run)
                save()
                if run["return_code"] != 0:
                    raise RuntimeError(f"Brandenburg command failed: {label}; see {result_path}")
                run["output"] = identity(output)
                passes = re.search(r"Needed (\d+) pass\(es\)", run["stderr"])
                run["input_passes"] = int(passes.group(1)) if passes else None
                run["reference_check"] = check_refs(output)
                pair.append(run)
                save()
                print(f"  {run['wall_seconds']:.3f} s; {run['input_passes']} input passes; check-refs exit {run['reference_check']['return_code']}", flush=True)
            same = (pair[0]["output"]["sha256"], pair[0]["output"]["bytes"]) == (pair[1]["output"]["sha256"], pair[1]["output"]["bytes"])
            result["comparisons"].append({"selector": selector, "strip_tags": strip, "identical_output": same})
            save()
            if not same:
                raise RuntimeError(f"Reference-complete output changed: {selector}, strip={strip}")
    result["all_outputs_identical"] = all(item["identical_output"] for item in result["comparisons"])
    result["all_output_references_complete"] = all(run["reference_check"]["return_code"] == 0 for run in result["brandenburg"])
    result["completed_at"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    save()
    print(json.dumps({key: result[key] for key in ("all_outputs_identical", "all_output_references_complete")}), flush=True)


if __name__ == "__main__":
    main()
