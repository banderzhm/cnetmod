#!/usr/bin/env python3
"""Render the full cross-language benchmark summary as a README PNG table."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


IMPLEMENTATIONS = {
    "cnetmod": "cnetmod", "statico-tokio-uring": "Statico / tokio-uring",
    "statico-monoio": "Statico / monoio", "rust-hyper": "Rust Hyper",
    "rust-monoio": "Rust monoio-h2", "rust-h3-quinn": "Rust Quinn / h3",
    "go-net-http": "Go net/http", "go-fasthttp": "Go fasthttp",
    "go-quic-go": "Go quic-go", "java26-jetty": "Java 26 / Jetty",
    "java26-virtual": "Java 26 / JDK virtual threads",
}
PROTOCOL_ORDER = {"HTTP/1.1": 0, "HTTPS/1.1": 1, "HTTP/2": 2, "HTTPS/2": 3, "HTTP/3": 4}
FONT_PATHS = ("C:/Windows/Fonts/segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    candidates = (("C:/Windows/Fonts/segoeuib.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf")
                  if bold else FONT_PATHS)
    for path in candidates:
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def implementation_name(scenario: str) -> str:
    for prefix, label in IMPLEMENTATIONS.items():
        if scenario == prefix or scenario.startswith(prefix + "-"):
            return label
    return scenario


def format_rps(value: float) -> str:
    if value >= 1_000_000:
        return f"{value / 1_000_000:.3f}M"
    if value >= 1_000:
        return f"{value / 1_000:.0f}K"
    return f"{value:.0f}"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("summary", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with args.summary.open(newline="", encoding="utf-8") as source:
        rows = list(csv.DictReader(source))
    rows.sort(key=lambda row: (PROTOCOL_ORDER[row["protocol"]], -float(row["rps_mean"])))

    values = []
    for row in rows:
        partial = row["status"] != "passed"
        values.append(([
            row["protocol"], implementation_name(row["scenario"]), format_rps(float(row["rps_mean"])),
            f"{float(row['p50_ms']):.3f}", f"{float(row['p99_ms']):.3f}",
            f"{float(row['success_rate']) * 100:.2f}%", "passed" if not partial else "partial — do not rank",
        ], partial))

    width, margin, title_height, row_height = 2880, 48, 186, 56
    column_widths = (300, 810, 300, 250, 250, 280, 594)
    height = title_height + row_height * (len(values) + 1) + margin
    image = Image.new("RGB", (width, height), "#ffffff")
    draw = ImageDraw.Draw(image)
    title_font, subtitle_font, header_font, text_font = font(42, True), font(26), font(25, True), font(24)
    draw.text((margin, 34), "Cross-language HTTP benchmark — Arch Linux / WSL2 — 2026-08-05", font=title_font, fill="#17365d")
    draw.text((margin, 94), "16 server CPUs + 16 client CPUs · oha 1.15.0 · three runs · /hello", font=subtitle_font, fill="#374151")
    draw.text((margin, 132), "Rows in red have incomplete success and must not be used as throughput rankings.", font=subtitle_font, fill="#b42318")

    headers = ("Protocol", "Implementation", "Mean req/s", "P50 ms", "P99 ms", "Success", "Result")
    top = title_height
    x = 0
    for label, cell_width in zip(headers, column_widths):
        draw.rectangle((x, top, x + cell_width, top + row_height), fill="#17365d")
        draw.text((x + 14, top + 13), label, font=header_font, fill="#ffffff")
        x += cell_width

    for index, (row, partial) in enumerate(values):
        y = top + row_height * (index + 1)
        fill = "#fff1f2" if partial else "#f8fafc" if index % 2 else "#ffffff"
        x = 0
        for column, (value, cell_width) in enumerate(zip(row, column_widths)):
            draw.rectangle((x, y, x + cell_width, y + row_height), fill=fill, outline="#d1d5db", width=1)
            color = "#b42318" if partial and column in (5, 6) else "#111827"
            draw.text((x + 14, y + 15), value, font=text_font, fill=color)
            x += cell_width

    args.output.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.output, "PNG", optimize=True)


if __name__ == "__main__":
    main()
