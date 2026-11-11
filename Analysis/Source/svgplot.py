"""Minimal, dependency-free SVG chart generator used by generate_analysis.py.

This project deliberately avoids adding third-party dependencies for
one-off tooling (the same reasoning M4's OBJ importer decision documents in
specs/milestones.md: a small, dependency-free implementation is preferable
to a heavyweight library when the required scope is narrow). matplotlib/
pandas are not available in this environment and are not required: M10 only
needs a handful of line/scatter charts with log-capable axes, which this
module implements directly as SVG text.

This is intentionally NOT a general-purpose plotting library. It supports
exactly what Analysis/Source/generate_analysis.py needs:
  - linear or log10 axis scaling (independently per axis)
  - multiple labelled series, each rendered as markers and/or a connecting
    polyline (solid or dashed, so "measured" and "theoretical reference"
    series are visually distinct per the M10 requirement)
  - a title, axis labels (with units baked into the label text), a legend,
    and simple tick labels
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import List, Optional, Sequence, Tuple

Point = Tuple[float, float]


@dataclass
class Series:
    name: str
    points: List[Point]
    color: str
    marker: bool = True
    line: bool = True
    dashed: bool = False
    marker_shape: str = "circle"  # circle | square | triangle


@dataclass
class Chart:
    title: str
    x_label: str
    y_label: str
    series: List[Series] = field(default_factory=list)
    x_log: bool = False
    y_log: bool = False
    width: int = 820
    height: int = 520
    note: Optional[str] = None  # small annotation printed under the title


_PALETTE = [
    "#1f77b4", "#d62728", "#2ca02c", "#9467bd",
    "#ff7f0e", "#8c564b", "#17becf", "#7f7f7f",
]


def _nice_ticks(vmin: float, vmax: float, log: bool, count: int = 6) -> List[float]:
    if log:
        lo = math.floor(math.log10(vmin))
        hi = math.ceil(math.log10(vmax))
        if hi == lo:
            hi += 1
        return [10.0 ** e for e in range(lo, hi + 1)]

    if vmax <= vmin:
        vmax = vmin + 1.0
    span = vmax - vmin
    raw_step = span / max(count, 1)
    magnitude = 10 ** math.floor(math.log10(raw_step)) if raw_step > 0 else 1.0
    for mult in (1, 2, 2.5, 5, 10):
        step = magnitude * mult
        if step >= raw_step:
            break
    start = math.floor(vmin / step) * step
    ticks = []
    v = start
    while v <= vmax + step * 0.5:
        ticks.append(v)
        v += step
    return ticks


def _fmt(v: float) -> str:
    if v == 0:
        return "0"
    if abs(v) >= 1000 or abs(v) < 0.001:
        return f"{v:.2e}"
    if abs(v) >= 100:
        return f"{v:.0f}"
    if abs(v) >= 1:
        return f"{v:.2f}"
    return f"{v:.4f}"


def render_svg(chart: Chart) -> str:
    margin_left = 90
    margin_right = 40
    margin_top = 70 if not chart.note else 90
    margin_bottom = 70
    plot_w = chart.width - margin_left - margin_right
    plot_h = chart.height - margin_top - margin_bottom

    all_x = [p[0] for s in chart.series for p in s.points]
    all_y = [p[1] for s in chart.series for p in s.points]
    if not all_x or not all_y:
        all_x, all_y = [0.0, 1.0], [0.0, 1.0]

    x_min, x_max = min(all_x), max(all_x)
    y_min, y_max = min(all_y), max(all_y)

    if chart.x_log:
        x_min = max(x_min, 1e-9)
        x_min = 10 ** math.floor(math.log10(x_min))
        x_max = 10 ** math.ceil(math.log10(x_max))
    else:
        pad = (x_max - x_min) * 0.08 or 1.0
        x_min -= pad
        x_max += pad

    if chart.y_log:
        y_min = max(y_min, 1e-9)
        y_min = 10 ** math.floor(math.log10(y_min))
        y_max = 10 ** math.ceil(math.log10(y_max))
    else:
        pad = (y_max - y_min) * 0.12 or 1.0
        y_min = max(0.0, y_min - pad)
        y_max += pad

    def sx(x: float) -> float:
        if chart.x_log:
            t = (math.log10(max(x, 1e-9)) - math.log10(x_min)) / (math.log10(x_max) - math.log10(x_min))
        else:
            t = (x - x_min) / (x_max - x_min)
        return margin_left + t * plot_w

    def sy(y: float) -> float:
        if chart.y_log:
            t = (math.log10(max(y, 1e-9)) - math.log10(y_min)) / (math.log10(y_max) - math.log10(y_min))
        else:
            t = (y - y_min) / (y_max - y_min)
        return margin_top + plot_h - t * plot_h

    parts: List[str] = []
    parts.append(
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{chart.width}" height="{chart.height}" '
        f'viewBox="0 0 {chart.width} {chart.height}" font-family="Helvetica,Arial,sans-serif">'
    )
    parts.append(f'<rect x="0" y="0" width="{chart.width}" height="{chart.height}" fill="white"/>')

    parts.append(
        f'<text x="{chart.width/2}" y="28" font-size="17" text-anchor="middle" font-weight="bold">'
        f'{_esc(chart.title)}</text>'
    )
    if chart.note:
        parts.append(
            f'<text x="{chart.width/2}" y="48" font-size="11" text-anchor="middle" fill="#555">'
            f'{_esc(chart.note)}</text>'
        )

    # Axes box
    parts.append(
        f'<rect x="{margin_left}" y="{margin_top}" width="{plot_w}" height="{plot_h}" '
        f'fill="none" stroke="#333" stroke-width="1"/>'
    )

    # Gridlines + ticks: X
    for xt in _nice_ticks(x_min, x_max, chart.x_log):
        if xt < x_min or xt > x_max:
            continue
        px = sx(xt)
        parts.append(f'<line x1="{px:.1f}" y1="{margin_top}" x2="{px:.1f}" y2="{margin_top+plot_h}" stroke="#eee"/>')
        parts.append(
            f'<text x="{px:.1f}" y="{margin_top+plot_h+18}" font-size="10" text-anchor="middle">{_fmt(xt)}</text>'
        )

    # Gridlines + ticks: Y
    for yt in _nice_ticks(y_min, y_max, chart.y_log):
        if yt < y_min or yt > y_max:
            continue
        py = sy(yt)
        parts.append(f'<line x1="{margin_left}" y1="{py:.1f}" x2="{margin_left+plot_w}" y2="{py:.1f}" stroke="#eee"/>')
        parts.append(
            f'<text x="{margin_left-8}" y="{py+3:.1f}" font-size="10" text-anchor="end">{_fmt(yt)}</text>'
        )

    # Axis labels
    parts.append(
        f'<text x="{margin_left+plot_w/2}" y="{chart.height-18}" font-size="12" text-anchor="middle">'
        f'{_esc(chart.x_label)}</text>'
    )
    parts.append(
        f'<text x="18" y="{margin_top+plot_h/2}" font-size="12" text-anchor="middle" '
        f'transform="rotate(-90 18 {margin_top+plot_h/2})">{_esc(chart.y_label)}</text>'
    )

    # Series
    for s in chart.series:
        pts = sorted(s.points, key=lambda p: p[0])
        if s.line and len(pts) > 1:
            path = " ".join(f'{sx(x):.1f},{sy(y):.1f}' for x, y in pts)
            dash = ' stroke-dasharray="6,5"' if s.dashed else ""
            parts.append(
                f'<polyline points="{path}" fill="none" stroke="{s.color}" stroke-width="2"{dash}/>'
            )
        if s.marker:
            for x, y in pts:
                cx, cy = sx(x), sy(y)
                if s.marker_shape == "square":
                    parts.append(f'<rect x="{cx-4:.1f}" y="{cy-4:.1f}" width="8" height="8" fill="{s.color}"/>')
                elif s.marker_shape == "triangle":
                    parts.append(
                        f'<polygon points="{cx:.1f},{cy-5:.1f} {cx-5:.1f},{cy+4:.1f} {cx+5:.1f},{cy+4:.1f}" '
                        f'fill="{s.color}"/>'
                    )
                else:
                    parts.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="4" fill="{s.color}"/>')

    # Legend
    legend_x = margin_left + 10
    legend_y = margin_top + 14
    for i, s in enumerate(chart.series):
        ly = legend_y + i * 16
        parts.append(f'<rect x="{legend_x}" y="{ly-8}" width="12" height="4" fill="{s.color}"/>')
        parts.append(f'<text x="{legend_x+18}" y="{ly-2}" font-size="11">{_esc(s.name)}</text>')

    parts.append('</svg>')
    return "\n".join(parts)


def _esc(text: str) -> str:
    return (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
    )


def next_color(index: int) -> str:
    return _PALETTE[index % len(_PALETTE)]


def save_chart(chart: Chart, path: str) -> None:
    with open(path, "w", encoding="utf-8") as f:
        f.write(render_svg(chart))
