#!/usr/bin/env python3
"""
runner/check_connectivity.py
============================
Pure-Python connectivity check for the tuned baseline, WITHOUT ns-3.

Network partition (a source and destination in different components) is the
single biggest sink of AODV PDR: those packets can never be delivered, by
any protocol. This script does not simulate AODV or measure PDR — it only
checks the geometric precondition: over many random placements of the nodes
in the field, is the unit-disk graph (edge iff distance <= range) connected,
and what is the mean node degree?

The transmission range in ns-3 depends on the propagation model and txPower,
so we sweep a plausible band. If the graph is reliably connected across the
band, the tuned scenario is not partition-limited and AODV's attack-free PDR
is bounded mainly by mobility and load, not by topology.

Usage:
    python3 check_connectivity.py                 # uses config.py values
    python3 check_connectivity.py --nodes 50 --area 800 --ranges 120,150,200,250
"""

import argparse
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import config  # noqa: E402


def components(adj):
    """Number of connected components via iterative DFS."""
    n = len(adj)
    seen = [False] * n
    comps = 0
    for s in range(n):
        if seen[s]:
            continue
        comps += 1
        stack = [s]
        seen[s] = True
        while stack:
            u = stack.pop()
            for v in np.nonzero(adj[u])[0]:
                if not seen[v]:
                    seen[v] = True
                    stack.append(int(v))
    return comps


def sample(n, area, rng_m, trials, seed0=0):
    connected = 0
    degrees = []
    reach_pairs = []  # fraction of random (s,d) pairs in the same component
    for t in range(trials):
        rng = np.random.default_rng(seed0 + t)
        pos = rng.uniform(0, area, (n, 2))
        d = np.linalg.norm(pos[:, None, :] - pos[None, :, :], axis=2)
        adj = (d <= rng_m) & (d > 0)
        degrees.append(adj.sum(axis=1).mean())
        c = components(adj)
        connected += (c == 1)
        # same-component fraction over a few random pairs
        comp_id = _label(adj)
        pairs = rng.integers(0, n, (50, 2))
        same = np.mean([comp_id[a] == comp_id[b] for a, b in pairs if a != b])
        reach_pairs.append(same)
    return (100.0 * connected / trials,
            float(np.mean(degrees)),
            100.0 * float(np.mean(reach_pairs)))


def _label(adj):
    n = len(adj)
    lab = [-1] * n
    cur = 0
    for s in range(n):
        if lab[s] != -1:
            continue
        stack = [s]
        lab[s] = cur
        while stack:
            u = stack.pop()
            for v in np.nonzero(adj[u])[0]:
                if lab[v] == -1:
                    lab[v] = cur
                    stack.append(int(v))
        cur += 1
    return lab


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nodes", type=int, default=config.SCENARIO["nNodes"])
    ap.add_argument("--area", type=float, default=config.SCENARIO["areaX"])
    ap.add_argument("--ranges", type=str, default="120,150,200,250")
    ap.add_argument("--trials", type=int, default=500)
    args = ap.parse_args()

    ranges = [float(x) for x in args.ranges.split(",")]
    print(f"Nodes={args.nodes}  area={args.area:.0f}x{args.area:.0f} m  "
          f"trials={args.trials}")
    print(f"Connectivity threshold ~ ln(N) = {np.log(args.nodes):.1f} (mean degree "
          f"should comfortably exceed this)\n")
    print(f"{'range (m)':>10} | {'mean degree':>12} | {'fully connected':>16} | "
          f"{'same-component pairs':>20}")
    print("-" * 68)
    for r in ranges:
        conn, deg, reach = sample(args.nodes, args.area, r, args.trials)
        print(f"{r:>10.0f} | {deg:>12.1f} | {conn:>15.1f}% | {reach:>19.1f}%")
    print("\nRead: 'fully connected' near 100% and 'same-component pairs' near "
          "100% mean the field is not partitioned, so the attack-free PDR is\n"
          "limited by mobility/load, not topology. This is a geometric check, "
          "not a PDR measurement — run the ns-3 scenario to get PDR.")


if __name__ == "__main__":
    main()
