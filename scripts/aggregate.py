#!/usr/bin/env python3
"""Aggregate benchmark stdout into per-(pad, label) summaries.

Reads the harness output (from a file argument or stdin), groups the per-run
lines by (pad, label), and reports the median across runs of every value each
line reports:

  - latency groups (baseline, *_lat): RTT p50, RTT p99, 1-way p50, and (for
    *_lat) the harness's own "+over floor" field
  - throughput groups (*_tp): throughput, plus the IQR (25th-75th percentile)

Stdlib only. Line shapes (see benchmarks/benchmark.cc report()):

  [baseline] pad=1 run=0 RTT p50=Xns p90=... p99=... p99.9=... max=... | 1-way p50~=...ns
  [spsc_lat] pad=1 run=0 RTT p50=... p99=... ... | 1-way p50~=...ns | +Yns over floor
  [spsc_tp ] pad=1 run=0 throughput=Z Mmsg/s
"""

import re
import sys
from collections import defaultdict
from statistics import median


def percentile(values, p):
    """Linear-interpolated percentile, p in [0, 100]. Empty -> None."""
    if not values:
        return None
    s = sorted(values)
    if len(s) == 1:
        return s[0]
    rank = (p / 100.0) * (len(s) - 1)
    lo = int(rank)
    hi = min(lo + 1, len(s) - 1)
    frac = rank - lo
    return s[lo] + (s[hi] - s[lo]) * frac


HEADER_RE = re.compile(r"\[\s*(?P<label>\S+)\s*\]\s+pad=(?P<pad>\d+)\s+run=\d+")
LAT_P50_RE = re.compile(r"RTT p50=(?P<v>[\d.]+)ns")
LAT_P99_RE = re.compile(r"[^.9]p99=(?P<v>[\d.]+)ns")  # not p99.9
ONEWAY_RE = re.compile(r"1-way p50~=(?P<v>[\d.]+)ns")
OVERFLOOR_RE = re.compile(r"\+(?P<v>[-\d.]+)ns over floor")
TP_RE = re.compile(r"throughput=(?P<v>[\d.]+) Mmsg/s")


def parse(lines):
    """-> dict[(pad, label)] -> dict[metric] -> list[float]."""
    groups = defaultdict(lambda: defaultdict(list))
    for line in lines:
        m = HEADER_RE.search(line)
        if not m:
            continue
        key = (int(m.group("pad")), m.group("label"))
        g = groups[key]

        tp = TP_RE.search(line)
        if tp:
            g["throughput"].append(float(tp.group("v")))
            continue

        for metric, rx in (
            ("rtt_p50", LAT_P50_RE),
            ("rtt_p99", LAT_P99_RE),
            ("oneway_p50", ONEWAY_RE),
            ("over_floor", OVERFLOOR_RE),
        ):
            hit = rx.search(line)
            if hit:
                g[metric].append(float(hit.group("v")))
    return groups


def fmt(x):
    return "n/a" if x is None else f"{x:.1f}"


def report(groups):
    for key in sorted(groups, key=lambda k: (k[0], k[1])):
        pad, label = key
        g = groups[key]
        runs = max((len(v) for v in g.values()), default=0)
        print(f"[pad={pad}] {label}  (n={runs} runs)")
        if "throughput" in g:
            vals = g["throughput"]
            med = median(vals)
            iqr_lo = percentile(vals, 25)
            iqr_hi = percentile(vals, 75)
            print(f"    throughput median = {fmt(med)} Mmsg/s")
            print(f"    throughput IQR    = {fmt(iqr_lo)} - {fmt(iqr_hi)} Mmsg/s")
        else:
            if "rtt_p50" in g:
                print(f"    RTT p50 median    = {fmt(median(g['rtt_p50']))} ns")
            if "rtt_p99" in g:
                print(f"    RTT p99 median    = {fmt(median(g['rtt_p99']))} ns")
            if "oneway_p50" in g:
                print(f"    1-way p50 median  = {fmt(median(g['oneway_p50']))} ns")
            if "over_floor" in g:
                print(f"    +over floor median= {fmt(median(g['over_floor']))} ns")
        print()


def main():
    if len(sys.argv) > 1 and sys.argv[1] not in ("-", "/dev/stdin"):
        with open(sys.argv[1]) as f:
            lines = f.readlines()
    else:
        lines = sys.stdin.readlines()
    report(parse(lines))


if __name__ == "__main__":
    main()
