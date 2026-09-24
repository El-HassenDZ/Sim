"""Post-simulation analysis of manet_scenario.py outputs.

Reads every ``*.meta.json`` found in the given directories, together with the
FlowMonitor XML and control-traffic CSV it references, and computes per-run
metrics, per-variant summaries with 95 % Student confidence intervals, and
optionally paired differences against a reference variant.

Metric definitions
------------------
* PDR: data packets received / data packets offered. "Offered" is the
  CbrSource attempted count, which includes packets the socket refused for
  lack of a route; FlowMonitor's txPackets would exclude them and favour
  proactive protocols.
* Goodput: received data payload bits over each flow's active period, summed
  over flows (kbit/s).
* Delay and jitter: FlowMonitor's delaySum and jitterSum over received data
  packets, pooled across flows (ms).
* Hop count: FlowMonitor's timesForwarded / rxPackets + 1. For reactive
  protocols it over-counts packets parked during a route discovery (they
  re-enter IP through the loopback device), so treat it as an upper bound.
* NRL (normalized routing load): routing control packets transmitted, every
  hop and every broadcast included, per data packet delivered. Control
  packets come from ControlTrafficMonitor, not FlowMonitor, which ignores
  broadcasts.

Pairing: runs sharing (config_id, seed, run) saw identical mobility and
traffic, so differences between variants are computed replication by
replication, which removes the topology-induced variance from the comparison.

Outputs (in --out, default: the first input directory)
------------------------------------------------------
* runs.csv: one row per run and metric set.
* summary.csv: one row per (config_id, variant) with mean, standard
  deviation and CI half-width of each metric.
* paired.csv: with --reference, mean paired difference and CI per variant.
A Markdown summary is also printed on standard output.

Usage
-----
    python3 analysis/analyze_results.py results/ --reference aodv
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import statistics
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable, Sequence

# Two-sided 95 % Student quantiles t(0.975, df). For degrees of freedom
# between tabulated values the next lower entry is used, which widens the
# interval slightly rather than understating uncertainty.
_T_975 = {
    1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571, 6: 2.447, 7: 2.365, 8: 2.306,
    9: 2.262, 10: 2.228, 11: 2.201, 12: 2.179, 13: 2.160, 14: 2.145, 15: 2.131,
    16: 2.120, 17: 2.110, 18: 2.101, 19: 2.093, 20: 2.086, 21: 2.080, 22: 2.074,
    23: 2.069, 24: 2.064, 25: 2.060, 26: 2.056, 27: 2.052, 28: 2.048, 29: 2.045,
    30: 2.042, 40: 2.021, 60: 2.000, 120: 1.980,
}  # fmt: skip

_TIME_PATTERN = re.compile(r"^([+-]?[0-9.eE+-]+?)(ns|us|ms|s|min|h|d)?$")
_TIME_UNIT_S = {"ns": 1e-9, "us": 1e-6, "ms": 1e-3, "s": 1.0, "min": 60.0, "h": 3600.0}


@dataclass
class RunMetrics:
    """Metrics of one simulation run."""

    tag: str
    config_id: str
    variant: str
    label: str
    seed: int
    run: int
    offered: int
    refused: int
    received: int
    pdr: float
    goodput_kbps: float
    delay_ms: float
    jitter_ms: float
    hops: float
    control_packets: int
    control_bytes: int
    nrl: float


METRICS = ("pdr", "goodput_kbps", "delay_ms", "jitter_ms", "hops", "nrl", "control_packets")


def t_quantile(df: int) -> float:
    """Return the two-sided 95 % Student quantile for df degrees of freedom."""
    if df < 1:
        return math.nan
    eligible = [k for k in _T_975 if k <= df]
    return _T_975[max(eligible)] if df <= 120 else 1.960


def parse_time_s(value: str) -> float:
    """Convert an ns-3 serialized Time (e.g. ``+1.5e+06ns``) to seconds.

    Args:
        value: The attribute text from the FlowMonitor XML.

    Returns:
        The duration in seconds.
    """
    match = _TIME_PATTERN.match(value.strip())
    if not match:
        raise ValueError(f"Unrecognized time value {value!r}")
    number, unit = match.groups()
    return float(number) * _TIME_UNIT_S[unit or "s"]


def mean_ci(values: Sequence[float]) -> tuple[float, float, float]:
    """Compute mean, sample standard deviation and 95 % CI half-width.

    Args:
        values: One value per replication.

    Returns:
        (mean, stdev, half_width); stdev and half_width are NaN below two values.
    """
    finite = [v for v in values if not math.isnan(v)]
    if not finite:
        return math.nan, math.nan, math.nan
    mean = statistics.fmean(finite)
    if len(finite) < 2:
        return mean, math.nan, math.nan
    stdev = statistics.stdev(finite)
    return mean, stdev, t_quantile(len(finite) - 1) * stdev / math.sqrt(len(finite))


def load_run(meta_path: Path) -> RunMetrics:
    """Compute the metrics of one run from its metadata and output files.

    Args:
        meta_path: Path of the run's ``.meta.json``.

    Returns:
        The run metrics.
    """
    meta = json.loads(meta_path.read_text(encoding="utf-8"))
    base = meta_path.parent
    params = meta["parameters"]
    flows = {flow["dst_port"]: flow for flow in meta["flows"]}

    root = ET.parse(base / meta["outputs"]["flowmon"]).getroot()
    classifier = {
        flow.get("flowId"): flow for flow in root.find("Ipv4FlowClassifier").findall("Flow")
    }
    received = forwarded = 0
    delay_s = jitter_s = goodput_bps = 0.0
    jitter_samples = 0
    accepted_seen = defaultdict(int)
    for stats in root.find("FlowStats").findall("Flow"):
        five_tuple = classifier[stats.get("flowId")]
        port = int(five_tuple.get("destinationPort"))
        if int(five_tuple.get("protocol")) != 17 or port not in flows:
            continue
        rx = int(stats.get("rxPackets"))
        received += rx
        forwarded += int(stats.get("timesForwarded"))
        delay_s += parse_time_s(stats.get("delaySum"))
        jitter_s += parse_time_s(stats.get("jitterSum"))
        jitter_samples += max(rx - 1, 0)
        accepted_seen[port] += int(stats.get("txPackets"))
        flow = flows[port]
        active_s = flow["stop_s"] - flow["start_s"]
        goodput_bps += rx * params["packet_size"] * 8 / active_s

    for port, flow in flows.items():
        if accepted_seen[port] != flow["accepted"]:
            print(
                f"warning: {meta['tag']} flow {port}: FlowMonitor saw "
                f"{accepted_seen[port]} packets, the source accepted {flow['accepted']}",
                file=sys.stderr,
            )

    control_packets = control_bytes = 0
    with open(base / meta["outputs"]["control"], newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            control_packets += int(row["control_tx_packets"])
            control_bytes += int(row["control_tx_bytes"])

    offered = sum(flow["attempted"] for flow in flows.values())
    return RunMetrics(
        tag=meta["tag"],
        config_id=meta["config_id"],
        variant=meta["protocol_variant"],
        label=params.get("label") or meta["protocol_variant"],
        seed=params["seed"],
        run=params["run"],
        offered=offered,
        refused=sum(flow["refused"] for flow in flows.values()),
        received=received,
        pdr=received / offered if offered else math.nan,
        goodput_kbps=goodput_bps / 1e3,
        delay_ms=1e3 * delay_s / received if received else math.nan,
        jitter_ms=1e3 * jitter_s / jitter_samples if jitter_samples else math.nan,
        hops=forwarded / received + 1 if received else math.nan,
        control_packets=control_packets,
        control_bytes=control_bytes,
        nrl=control_packets / received if received else math.nan,
    )


def collect_runs(directories: Iterable[Path]) -> list[RunMetrics]:
    """Load every run found under the given directories.

    Runs whose files are missing or unreadable are reported and skipped, so
    that a partially failed campaign can still be analysed.

    Args:
        directories: Directories searched recursively for ``*.meta.json``.

    Returns:
        The runs, sorted by variant then replication.
    """
    runs = []
    for directory in directories:
        for meta_path in sorted(directory.rglob("*.meta.json")):
            try:
                runs.append(load_run(meta_path))
            except (OSError, KeyError, ValueError, ET.ParseError) as error:
                print(f"warning: skipping {meta_path}: {error}", file=sys.stderr)
    runs.sort(key=lambda r: (r.config_id, r.label, r.seed, r.run))
    return runs


def summarize(runs: Sequence[RunMetrics]) -> list[dict]:
    """Aggregate runs by (config_id, variant).

    Args:
        runs: Per-run metrics.

    Returns:
        One dictionary per group with n and mean/std/ci of every metric.
    """
    groups: dict[tuple[str, str, str], list[RunMetrics]] = defaultdict(list)
    for run in runs:
        groups[(run.config_id, run.variant, run.label)].append(run)
    summary = []
    for (config, variant, label), members in sorted(groups.items()):
        row = {"config_id": config, "variant": variant, "label": label, "n": len(members)}
        for metric in METRICS:
            mean, stdev, half = mean_ci([float(getattr(r, metric)) for r in members])
            row[f"{metric}_mean"] = mean
            row[f"{metric}_std"] = stdev
            row[f"{metric}_ci95"] = half
        summary.append(row)
    return summary


def paired_differences(runs: Sequence[RunMetrics], reference: str) -> list[dict]:
    """Compare each variant with a reference, replication by replication.

    Args:
        runs: Per-run metrics.
        reference: Label or variant name of the reference.

    Returns:
        One dictionary per (config_id, variant) with the number of pairs and
        mean/ci of the difference (variant - reference) for every metric.
    """
    by_key = {(r.config_id, r.label, r.seed, r.run): r for r in runs}
    ref_runs = [r for r in runs if reference in (r.label, r.variant)]
    if not ref_runs:
        raise SystemExit(f"Reference {reference!r} not found among the runs")
    ref_label = ref_runs[0].label
    others = sorted({(r.config_id, r.label) for r in runs if r.label != ref_label})
    rows = []
    for config, label in others:
        pairs = [
            (by_key[(config, label, r.seed, r.run)], r)
            for r in ref_runs
            if r.config_id == config and (config, label, r.seed, r.run) in by_key
        ]
        if not pairs:
            continue
        row = {"config_id": config, "label": label, "reference": ref_label, "pairs": len(pairs)}
        for metric in METRICS:
            diffs = [float(getattr(a, metric)) - float(getattr(b, metric)) for a, b in pairs]
            mean, _, half = mean_ci(diffs)
            row[f"{metric}_diff"] = mean
            row[f"{metric}_ci95"] = half
        rows.append(row)
    return rows


def write_csv(path: Path, rows: Sequence[dict]) -> None:
    """Write dictionaries sharing the same keys as a CSV file."""
    if not rows:
        return
    with open(path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def format_ci(mean: float, half: float, digits: int) -> str:
    """Render ``mean ± half`` with a fixed number of decimals."""
    if math.isnan(mean):
        return "n/a"
    if math.isnan(half):
        return f"{mean:.{digits}f}"
    return f"{mean:.{digits}f} ± {half:.{digits}f}"


def print_markdown(summary: Sequence[dict], paired: Sequence[dict]) -> None:
    """Print the summary, and the paired comparison if any, as Markdown tables."""
    print("| config | variant | n | PDR | goodput (kb/s) | delay (ms) | NRL | hops |")
    print("|---|---|---|---|---|---|---|---|")
    for row in summary:
        print(
            f"| {row['config_id']} | {row['label']} | {row['n']} "
            f"| {format_ci(row['pdr_mean'], row['pdr_ci95'], 3)} "
            f"| {format_ci(row['goodput_kbps_mean'], row['goodput_kbps_ci95'], 1)} "
            f"| {format_ci(row['delay_ms_mean'], row['delay_ms_ci95'], 1)} "
            f"| {format_ci(row['nrl_mean'], row['nrl_ci95'], 2)} "
            f"| {format_ci(row['hops_mean'], row['hops_ci95'], 2)} |"
        )
    if not paired:
        return
    print()
    print(f"Paired differences against {paired[0]['reference']} (variant - reference):")
    print()
    print("| config | variant | pairs | ΔPDR | Δdelay (ms) | ΔNRL |")
    print("|---|---|---|---|---|---|")
    for row in paired:
        print(
            f"| {row['config_id']} | {row['label']} | {row['pairs']} "
            f"| {format_ci(row['pdr_diff'], row['pdr_ci95'], 3)} "
            f"| {format_ci(row['delay_ms_diff'], row['delay_ms_ci95'], 1)} "
            f"| {format_ci(row['nrl_diff'], row['nrl_ci95'], 2)} |"
        )


def main(argv: Sequence[str]) -> int:
    """Entry point.

    Args:
        argv: Command-line arguments, program name excluded.

    Returns:
        The process exit status.
    """
    parser = argparse.ArgumentParser(description="Analyse manet_scenario.py results.")
    parser.add_argument("directories", nargs="+", type=Path)
    parser.add_argument("--reference", help="Label or variant to compare the others against")
    parser.add_argument("--out", type=Path, help="Directory for the CSV files")
    args = parser.parse_args(argv)

    runs = collect_runs(args.directories)
    if not runs:
        raise SystemExit("No run found")
    out = args.out or args.directories[0]
    out.mkdir(parents=True, exist_ok=True)
    summary = summarize(runs)
    paired = paired_differences(runs, args.reference) if args.reference else []
    write_csv(out / "runs.csv", [asdict(r) for r in runs])
    write_csv(out / "summary.csv", summary)
    write_csv(out / "paired.csv", paired)
    print_markdown(summary, paired)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
