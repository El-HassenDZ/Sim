#!/usr/bin/env python3
"""
runner/run_experiment.py
========================
Drive the ns-3.48 AODV attack scenario: install the scratch program into an
ns-3 tree, build it, run the baseline and each attack for N repetitions,
then aggregate every per-run *_metrics.csv into one summary with mean and
95 % confidence intervals, and print a baseline-vs-attack comparison.

This runner does NOT simulate anything itself: all numbers come from ns-3.
It only orchestrates and aggregates. It therefore needs a working ns-3.48
build environment (a checkout with ./ns3).

Usage:
    python3 run_experiment.py --ns3-dir /path/to/ns-3.48
    python3 run_experiment.py --ns3-dir /path/to/ns-3.48 --runs 5 --attacks blackhole,flood
    python3 run_experiment.py --ns3-dir /path/to/ns-3.48 --no-build   # reuse last build
"""

import argparse
import csv
import math
import os
import shutil
import statistics
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import config  # noqa: E402

SCRATCH_SRC = os.path.join(PROJECT, "scratch", config.TARGET)


def t_quantile_95(df):
    """Two-sided 95 % Student-t quantile (df >= 1), small table + normal tail."""
    table = {1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571, 6: 2.447,
             7: 2.365, 8: 2.306, 9: 2.262, 10: 2.228, 12: 2.179, 15: 2.131,
             20: 2.086, 25: 2.060, 30: 2.042}
    if df in table:
        return table[df]
    keys = sorted(table)
    if df < keys[0]:
        return table[keys[0]]
    if df > keys[-1]:
        return 1.96
    lo = max(k for k in keys if k <= df)
    return table[lo]


def install_scratch(ns3_dir):
    dst = os.path.join(ns3_dir, "scratch", config.TARGET)
    if os.path.abspath(dst) == os.path.abspath(SCRATCH_SRC):
        return
    if os.path.islink(dst) or os.path.isfile(dst):
        os.remove(dst)
    elif os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(SCRATCH_SRC, dst)
    print(f"[install] copied scratch/{config.TARGET} -> {dst}")


def build(ns3_dir):
    print("[build] ./ns3 build")
    subprocess.run(["./ns3", "build", config.TARGET], cwd=ns3_dir, check=True)


def run_one(ns3_dir, out_prefix, extra):
    args = " ".join(f"--{k}={v}" for k, v in extra.items())
    cmd = f'{config.TARGET} {args} --out={out_prefix}'
    print(f"[run] {args}")
    subprocess.run(["./ns3", "run", cmd], cwd=ns3_dir, check=True)


def read_metrics(path):
    with open(path, newline="") as f:
        return next(csv.DictReader(f))


def aggregate(rows, out_csv):
    """rows: list of dicts (one per run). Group by (mode, attack)."""
    numeric = [k for k in rows[0]
               if k not in ("mode", "attack", "nNodes", "nMalicious", "run")]
    groups = {}
    for r in rows:
        groups.setdefault((r["mode"], r["attack"]), []).append(r)

    summary = []
    for (mode, attack), grp in groups.items():
        entry = {"mode": mode, "attack": attack, "n_runs": len(grp)}
        for k in numeric:
            vals = [float(r[k]) for r in grp]
            entry[f"{k}_mean"] = statistics.mean(vals)
            if len(vals) >= 2:
                sd = statistics.stdev(vals)
                entry[f"{k}_ci95"] = t_quantile_95(len(vals) - 1) * sd / math.sqrt(len(vals))
            else:
                entry[f"{k}_ci95"] = float("nan")
        summary.append(entry)

    fields = ["mode", "attack", "n_runs"] + \
             [f"{k}_{s}" for k in numeric for s in ("mean", "ci95")]
    with open(out_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for e in summary:
            w.writerow(e)
    print(f"[summary] -> {out_csv}")
    return summary


def print_comparison(summary):
    base = next((e for e in summary if e["mode"] == "baseline"), None)
    cols = [("pdr_percent", "PDR %"), ("throughput_kbps", "TP kbps"),
            ("avg_delay_ms", "Delay ms"), ("norm_routing_overhead", "NRO"),
            ("energy_consumed_J", "Energy J")]
    print("\n" + "=" * 90)
    print("  Baseline vs attacks (mean ± 95 % CI over runs)")
    print("=" * 90)
    header = f"  {'scenario':<20}" + "".join(f"{c[1]:>16}" for c in cols)
    print(header)
    print("  " + "-" * (len(header) - 2))
    for e in summary:
        label = e["attack"] if e["mode"] == "attack" else "baseline"
        cells = ""
        for k, _ in cols:
            m, ci = e[f"{k}_mean"], e[f"{k}_ci95"]
            cells += f"{m:>9.2f}±{ci:<6.2f}"
        print(f"  {label:<20}{cells}")
    if base:
        print("\n  Impact of each attack on PDR (percentage points vs baseline):")
        for e in summary:
            if e["mode"] != "attack":
                continue
            delta = e["pdr_percent_mean"] - base["pdr_percent_mean"]
            print(f"    {e['attack']:<12} {delta:+.2f} pp")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ns3-dir", required=True, help="path to ns-3.48 root (has ./ns3)")
    ap.add_argument("--runs", type=int, default=config.N_RUNS)
    ap.add_argument("--attacks", type=str, default=",".join(config.ATTACKS))
    ap.add_argument("--out-dir", type=str, default=os.path.join(PROJECT, "outputs"))
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--no-install", action="store_true", help="scratch already in the tree")
    args = ap.parse_args()

    ns3_dir = os.path.abspath(args.ns3_dir)
    if not os.path.isfile(os.path.join(ns3_dir, "ns3")):
        sys.exit(f"error: {ns3_dir} does not look like an ns-3 tree (no ./ns3)")
    os.makedirs(args.out_dir, exist_ok=True)
    attacks = [a for a in args.attacks.split(",") if a]

    if not args.no_install:
        install_scratch(ns3_dir)
    if not args.no_build:
        build(ns3_dir)

    rows = []
    for run in range(1, args.runs + 1):
        # Baseline reference (attack-free).
        base_prefix = os.path.join(args.out_dir, f"baseline_r{run}")
        run_one(ns3_dir, base_prefix,
                {**config.SCENARIO, "mode": "baseline", "run": run})
        rows.append(read_metrics(base_prefix + "_metrics.csv"))

        # Attacked runs (same run number = same topology/mobility/traffic).
        for attack in attacks:
            pref = os.path.join(args.out_dir, f"{attack}_r{run}")
            run_one(ns3_dir, pref,
                    {**config.SCENARIO, "mode": "attack", "attack": attack,
                     "nMalicious": config.N_MALICIOUS,
                     "grayholeProb": config.GRAYHOLE_PROB, "run": run})
            rows.append(read_metrics(pref + "_metrics.csv"))

    summary = aggregate(rows, os.path.join(args.out_dir, "summary.csv"))
    print_comparison(summary)


if __name__ == "__main__":
    main()
