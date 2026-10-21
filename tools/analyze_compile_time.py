#!/usr/bin/env python3
"""Summarize Clang -ftime-trace files and Ninja command timings (no dependencies).

Use a fresh build directory: Ninja logs accumulate across incremental builds.
Trace totals are overlapping measurements, not additive wall-clock costs.
"""

import argparse
import collections
import functools
import json
from pathlib import Path


def analyze(build):
    commands = {}
    log = build / ".ninja_log"
    if log.exists():
        for line in log.read_text().splitlines():
            if line.startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) < 5:
                continue
            start, end, _, output, signature = fields[:5]
            # An object and its PCM can be outputs of the same command.
            key = (int(start), int(end), signature)
            commands.setdefault(key, []).append(output)
    timings = [
        {"seconds": (end - start) / 1000, "outputs": outputs}
        for (start, end, _), outputs in commands.items()
    ]
    timings.sort(key=lambda item: item["seconds"], reverse=True)
    duration_by_output = {
        output: item["seconds"] for item in timings for output in item["outputs"]
    }
    categories = collections.Counter()
    for item in timings:
        outputs = item["outputs"]
        if any(name.endswith(".ddi") for name in outputs):
            category = "dependency_scan"
        elif any(name.startswith("third_party/") for name in outputs):
            category = "third_party"
        elif any(name.endswith(".pcm") for name in outputs):
            category = "module_interface_and_object"
        elif any(name.endswith(".o") for name in outputs):
            category = "implementation_or_regular_object"
        else:
            category = "other"
        categories[category] += item["seconds"]
    providers = {}
    import_counts = collections.Counter()
    for path in build.rglob("*.ddi"):
        for rule in json.loads(path.read_text()).get("rules", []):
            requires = [entry["logical-name"] for entry in rule.get("requires", [])]
            import_counts.update(requires)
            for entry in rule.get("provides", []):
                providers[entry["logical-name"]] = (
                    rule.get("primary-output", ""), requires)

    @functools.lru_cache(None)
    def chain(name):
        output, dependencies = providers[name]
        previous = max((chain(dep) for dep in dependencies if dep in providers),
                       key=lambda item: item[0], default=(0, []))
        return previous[0] + duration_by_output.get(output, 0), previous[1] + [name]

    longest = max((chain(name) for name in providers),
                  key=lambda item: item[0], default=(0, []))
    totals = collections.Counter()
    imports = collections.Counter()
    traces = []
    for path in build.rglob("*.json"):
        # CMake's compilation database and module metadata are not traces.
        if path.name in {"compile_commands.json", "CXXModules.json"}:
            continue
        try:
            data = json.loads(path.read_text())
        except (ValueError, UnicodeError):
            continue
        if not isinstance(data, dict) or "traceEvents" not in data:
            continue
        local = {}
        for event in data["traceEvents"]:
            name = event.get("name", "")
            duration = event.get("dur", 0) / 1_000_000
            if name.startswith("Total "):
                totals[name[6:]] += duration
                local[name[6:]] = duration
            if name in {"ReadAST", "ReadModule"}:
                imports[event.get("args", {}).get("detail", "unknown")] += duration
        traces.append({"file": str(path.relative_to(build)), **local})
    traces.sort(key=lambda item: item.get("ExecuteCompiler", 0), reverse=True)
    pcm = sorted(
        ({"file": str(p.relative_to(build)), "bytes": p.stat().st_size}
         for p in build.rglob("*.pcm")),
        key=lambda item: item["bytes"], reverse=True,
    )
    return {
        "build": str(build.resolve()),
        "command_count": len(timings),
        "command_seconds_sum": sum(item["seconds"] for item in timings),
        "command_seconds_by_category": dict(categories),
        "direct_import_count": dict(import_counts.most_common()),
        "longest_module_chain_command_seconds": longest[0],
        "longest_module_chain": longest[1],
        "slowest_commands": timings[:40],
        "trace_count": len(traces),
        "trace_total_seconds_overlapping": dict(totals.most_common()),
        "module_read_seconds_inclusive": dict(imports.most_common(40)),
        "slowest_traces": traces[:40],
        "pcm_count": len(pcm),
        "pcm_bytes": sum(item["bytes"] for item in pcm),
        "largest_pcm": pcm[:40],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not (args.build / ".ninja_log").is_file():
        parser.error("build must contain a completed Ninja build's .ninja_log")
    report = json.dumps(analyze(args.build), indent=2) + "\n"
    if args.output:
        args.output.write_text(report)
    else:
        print(report, end="")


if __name__ == "__main__":
    main()
