"""Summarize same-binary gcannon calibration without rounded throughput values."""

import pathlib
import re
import statistics
import sys


def read_round(directory, workers, round_number, label):
    prefix = directory / f"w{workers}-r{round_number}-{label}"
    output = prefix.with_suffix(".load.txt").read_text()
    match = re.search(r"(\d+) requests in ([\d.]+)s, (\d+) responses", output)
    if match is None:
        raise ValueError(f"Missing response summary in {prefix}")
    responses = int(match[3])
    statuses = re.search(r"2xx=(\d+), 3xx=(\d+), 4xx=(\d+), 5xx=(\d+)", output)
    if statuses is None or int(statuses[1]) != responses or any(
        int(value) for value in statuses.groups()[1:]
    ):
        raise ValueError(f"Unsuccessful responses in {prefix}")
    errors = re.search(r"Errors: connect (\d+), read (\d+), timeout (\d+)", output)
    if errors and any(int(value) for value in errors.groups()):
        raise ValueError(f"Generator error in {prefix}")
    ticks, ticks_per_second = map(int, prefix.with_suffix(".cpu.txt").read_text().split())
    return responses / float(match[2]), ticks / ticks_per_second * 1_000_000 / responses


directory = pathlib.Path(sys.argv[1])
worker_counts = sorted({int(path.name.split("-")[0][1:])
                        for path in directory.glob("w*-r1-A.load.txt")})
for workers in worker_counts:
    print("| Workers | Pair | A RPS | B RPS | B/A change | A CPU us/request | B CPU us/request |")
    print("|---|---|---:|---:|---:|---:|---:|")
    changes = []
    rates = []
    for round_number in (1, 2, 3):
        rate_a, cpu_a = read_round(directory, workers, round_number, "A")
        rate_b, cpu_b = read_round(directory, workers, round_number, "B")
        change = (rate_b / rate_a - 1) * 100
        changes.append(change)
        rates.extend((rate_a, rate_b))
        print(f"| {workers} | {round_number} | {rate_a:.0f} | {rate_b:.0f} | "
              f"{change:+.2f}% | {cpu_a:.3f} | {cpu_b:.3f} |")
    spread = (max(rates) / min(rates) - 1) * 100
    print(f"\n{workers} workers: paired median {statistics.median(changes):+.2f}%, "
          f"largest absolute paired change {max(map(abs, changes)):.2f}%, "
          f"six-run range {spread:.2f}%.\n")
