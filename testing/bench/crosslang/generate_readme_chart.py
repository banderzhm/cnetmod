#!/usr/bin/env python3
"""Render the comparable cross-language throughput result as a README chart."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.patches import Patch


LABELS = {
    "cnetmod": "cnetmod", "statico-tokio-uring": "Statico tokio-uring",
    "statico-monoio": "Statico monoio", "rust-hyper": "Rust Hyper",
    "rust-monoio": "Rust monoio-h2", "rust-h3-quinn": "Rust Quinn/h3",
    "go-net-http": "Go net/http", "go-fasthttp": "Go fasthttp",
    "go-quic-go": "Go quic-go", "java26-jetty": "Java 26 Jetty",
    "java26-virtual": "Java 26 JDK virtual threads",
}
COLORS = {"cnetmod": "#135f83", "statico": "#52796f", "rust": "#d97706", "go": "#00a36c", "java26": "#7c3aed"}
PROTOCOLS = ("HTTP/1.1", "HTTPS/1.1", "HTTP/2", "HTTPS/2", "HTTP/3")


def prefix(value: str) -> str:
    for key in LABELS:
        if value == key or value.startswith(key + "-"):
            return key
    return value


def label(value: str) -> str:
    return LABELS[prefix(value)]


def color(value: str) -> str:
    root = prefix(value)
    return next((tone for category, tone in COLORS.items() if root.startswith(category)), "#64748b")


def rate(value: float) -> str:
    return f"{value / 1_000_000:.2f}M" if value >= 1_000_000 else f"{value / 1_000:.0f}K" if value >= 1_000 else f"{value:.0f}"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("summary", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with args.summary.open(newline="", encoding="utf-8") as source:
        rows = list(csv.DictReader(source))

    figure, axes = plt.subplots(1, len(PROTOCOLS), figsize=(22, 8.5))
    figure.subplots_adjust(left=0.055, right=0.995, top=0.85, bottom=0.18, wspace=0.85)
    figure.suptitle("Cross-language HTTP throughput — Arch Linux / WSL2 — 2026-08-05", fontsize=20, fontweight="bold", y=0.975)
    figure.text(0.5, 0.925, "16 server CPUs + 16 client CPUs · oha 1.15.0 · three runs · /hello", ha="center", fontsize=11)
    for axis, protocol in zip(axes, PROTOCOLS):
        items = [row for row in rows if row["protocol"] == protocol]
        items.sort(key=lambda row: float(row["rps_mean"]), reverse=True)
        values = [float(row["rps_mean"]) for row in items]
        labels = [label(row["scenario"]) for row in items]
        bars = axis.barh(range(len(items)), values, color=[color(row["scenario"]) for row in items])
        axis.invert_yaxis()
        axis.set_yticks(range(len(items)), labels, fontsize=9)
        axis.set_title(protocol, fontweight="bold", pad=10)
        axis.grid(axis="x", color="#d1d5db", linewidth=0.7)
        axis.set_axisbelow(True)
        axis.spines[["top", "right", "left"]].set_visible(False)
        axis.tick_params(axis="y", length=0)
        axis.ticklabel_format(axis="x", style="plain", useOffset=False)
        for bar, row, value in zip(bars, items, values):
            partial = row["status"] != "passed"
            if partial:
                bar.set_color("#dc2626")
                bar.set_hatch("///")
                bar.set_edgecolor("#991b1b")
            axis.text(value, bar.get_y() + bar.get_height() / 2, f" {rate(value)}", va="center", fontsize=8.5)
        if protocol == "HTTP/3":
            axis.text(0.5, -0.20, "Jetty HTTP/3: starts, then times out (not plotted)", transform=axis.transAxes,
                      ha="center", va="top", color="#b42318", fontsize=8.5)
        axis.set_xlabel("req/s", fontsize=9)
    figure.legend(handles=[
        Patch(facecolor="#135f83", label="cnetmod"), Patch(facecolor="#52796f", label="Statico"),
        Patch(facecolor="#d97706", label="Rust"), Patch(facecolor="#00a36c", label="Go"),
        Patch(facecolor="#7c3aed", label="Java 26 / Jetty"),
        Patch(facecolor="#dc2626", hatch="///", label="Partial result — do not rank"),
    ], loc="lower center", ncols=6, bbox_to_anchor=(0.5, -0.01), frameon=False)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(args.output, dpi=180, bbox_inches="tight")


if __name__ == "__main__":
    main()
