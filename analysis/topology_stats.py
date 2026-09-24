"""Protocol-independent topology statistics from mobility traces.

For every data flow of a run, the unit-disk graph induced by the node
positions is sampled at regular instants over the flow's active period, and
the shortest path between source and destination is computed. Two figures
come out:

* Connectivity: the fraction of samples in which a path exists at all. No
  routing protocol can deliver a packet while its endpoints are partitioned,
  so this is an upper bound on the achievable delivery ratio (MAC losses and
  queueing aside).
* Shortest-path length: the mean hop count over connected samples, and its
  distribution. Unlike FlowMonitor's timesForwarded, it is unaffected by the
  loopback detour of packets parked by reactive protocols, which makes it the
  right evidence that the scenario is genuinely multi-hop.

Requirements: runs made with ``--mobility-trace`` and ``--loss range`` (the
unit-disk assumption is only exact for RangePropagationLossModel). Mobility
is identical across protocols for a given (config_id, seed, run), so one
trace per replication covers every variant.

Usage
-----
    python3 analysis/topology_stats.py results/topology [--step 0.5]
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from bisect import bisect_right
from collections import Counter, deque
from pathlib import Path
from typing import Sequence

from analyze_results import format_ci, mean_ci

_LINE = re.compile(
    r"now=\+?(?P<t>[0-9.eE+-]+)ns node=(?P<node>\d+) "
    r"pos=(?P<x>[-0-9.eE+]+):(?P<y>[-0-9.eE+]+):[-0-9.eE+]+ "
    r"vel=(?P<vx>[-0-9.eE+]+):(?P<vy>[-0-9.eE+]+):[-0-9.eE+]+"
)


class Trajectories:
    """Piecewise-linear node trajectories rebuilt from CourseChange records."""

    def __init__(self, trace: Path) -> None:
        """Parse an ns-3 mobility ASCII trace.

        Args:
            trace: File written by MobilityHelper::EnableAsciiAll.
        """
        legs: dict[int, list[tuple[float, float, float, float, float]]] = {}
        with open(trace, encoding="utf-8") as handle:
            for line in handle:
                match = _LINE.match(line.strip())
                if not match:
                    continue
                t = float(match["t"]) * 1e-9
                record = (
                    t,
                    float(match["x"]),
                    float(match["y"]),
                    float(match["vx"]),
                    float(match["vy"]),
                )
                legs.setdefault(int(match["node"]), []).append(record)
        # Several course changes can share a timestamp (placement, then model
        # initialization at t = 0); the stable sort keeps the last one last,
        # and bisect_right below selects it.
        self._legs = {node: sorted(records, key=lambda r: r[0]) for node, records in legs.items()}
        self._times = {node: [r[0] for r in records] for node, records in self._legs.items()}

    @property
    def nodes(self) -> list[int]:
        """Return the node identifiers present in the trace."""
        return sorted(self._legs)

    def position(self, node: int, t: float) -> tuple[float, float]:
        """Return the position of a node at time t (seconds)."""
        index = bisect_right(self._times[node], t) - 1
        if index < 0:
            raise ValueError(f"No position known for node {node} at {t} s")
        t0, x, y, vx, vy = self._legs[node][index]
        return x + vx * (t - t0), y + vy * (t - t0)


def shortest_hops(
    positions: dict[int, tuple[float, float]], radius: float, source: int
) -> dict[int, int]:
    """Breadth-first hop distances from one node in the unit-disk graph.

    Args:
        positions: Node positions at the sampling instant.
        radius: Communication range.
        source: Starting node.

    Returns:
        Hop count of every node reachable from source.
    """
    r2 = radius * radius
    distance = {source: 0}
    queue = deque([source])
    while queue:
        current = queue.popleft()
        cx, cy = positions[current]
        for other, (ox, oy) in positions.items():
            if other not in distance and (ox - cx) ** 2 + (oy - cy) ** 2 <= r2:
                distance[other] = distance[current] + 1
                queue.append(other)
    return distance


def analyse_run(meta_path: Path, step: float) -> dict:
    """Sample connectivity and shortest paths of every flow of one run.

    Args:
        meta_path: Metadata of a run made with --mobility-trace.
        step: Sampling period in seconds.

    Returns:
        Per-run connectivity, mean hop count and hop count histogram.
    """
    meta = json.loads(meta_path.read_text(encoding="utf-8"))
    params = meta["parameters"]
    if params["loss"] != "range":
        raise ValueError("the unit-disk graph is only exact with --loss range")
    if "mobility" not in meta["outputs"]:
        raise ValueError("run made without --mobility-trace")
    trajectories = Trajectories(meta_path.parent / meta["outputs"]["mobility"])

    samples = connected = 0
    histogram: Counter[int] = Counter()
    for flow in meta["flows"]:
        t = flow["start_s"]
        while t < flow["stop_s"]:
            positions = {n: trajectories.position(n, t) for n in trajectories.nodes}
            hops = shortest_hops(positions, params["range"], flow["src_node"]).get(flow["dst_node"])
            samples += 1
            if hops is not None:
                connected += 1
                histogram[hops] += 1
            t += step
    total_hops = sum(h * c for h, c in histogram.items())
    return {
        "tag": meta["tag"],
        "config_id": meta["config_id"],
        "connectivity": connected / samples if samples else math.nan,
        "mean_hops": total_hops / connected if connected else math.nan,
        "histogram": histogram,
        "connected": connected,
    }


def main(argv: Sequence[str]) -> int:
    """Entry point.

    Args:
        argv: Command-line arguments, program name excluded.

    Returns:
        The process exit status.
    """
    parser = argparse.ArgumentParser(description="Topology statistics from mobility traces.")
    parser.add_argument("directories", nargs="+", type=Path)
    parser.add_argument("--step", type=float, default=1.0, help="Sampling period (s)")
    args = parser.parse_args(argv)

    runs = []
    for directory in args.directories:
        for meta_path in sorted(directory.rglob("*.meta.json")):
            try:
                runs.append(analyse_run(meta_path, args.step))
            except (OSError, KeyError, ValueError) as error:
                print(f"warning: skipping {meta_path}: {error}", file=sys.stderr)
    if not runs:
        raise SystemExit("No usable run found")

    connectivity = mean_ci([r["connectivity"] for r in runs])
    hops = mean_ci([r["mean_hops"] for r in runs])
    pooled: Counter[int] = Counter()
    for run in runs:
        pooled.update(run["histogram"])
    total = sum(pooled.values())
    print(f"runs: {len(runs)}")
    print(f"source-destination connectivity: {format_ci(connectivity[0], connectivity[2], 3)}")
    print(f"shortest path length (hops):     {format_ci(hops[0], hops[2], 2)}")
    print("hop count distribution over connected samples:")
    for h in sorted(pooled):
        print(f"  {h:2d} hop(s): {pooled[h] / total:6.1%}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
