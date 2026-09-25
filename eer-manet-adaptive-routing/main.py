#!/usr/bin/env python3
"""
main.py — AT-AEES-MANET
========================
Adaptive Trust-Aware Energy-Efficient Secure Routing in MANETs
with Attack-Resilient Trust Evaluation

For every run (seed):
  1. the router (GNN weights) is trained by HLOA on an independent
     training scenario (seed + TRAIN_SEED_OFFSET);
  2. every variant in config.VARIANTS is simulated on the same test
     scenario (seed): same topology, mobility, flows and random draws.
The ablation table is computed from these runs; nothing is hard-coded.

Run modes:
  python main.py               # 10 runs, 100 nodes, all variants
  python main.py --quick       # 2 runs, 20 nodes, 10 s
  python main.py --no-plot     # skip figure generation
  python main.py --variants full,no_defense
"""

import argparse
import json
import os
import sys

# Small matrices: multithreaded BLAS is ~5-20x slower here (measured).
for _var in ('OPENBLAS_NUM_THREADS', 'OMP_NUM_THREADS', 'MKL_NUM_THREADS'):
    os.environ.setdefault(_var, '1')

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'src'))   # packages live in src/
sys.path.insert(0, HERE)                         # config.py

import numpy as np                                                    # noqa: E402

import config                                                         # noqa: E402
from simulation.manet_env import ATMANETSimulation                    # noqa: E402
from evaluation.metrics import (aggregate, paired_vs, print_paired,   # noqa: E402
                                print_results)
from utils.helpers import get_logger, Timer, save_results, plot_results  # noqa: E402

logger = get_logger("AT-AEES-MANET")


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument('--quick', action='store_true',
                   help='Quick mode: 20 nodes, 2 runs, 10 s')
    p.add_argument('--no-plot', action='store_true')
    p.add_argument('--ablation', action='store_true',
                   help='Print paired differences vs the full system')
    p.add_argument('--seed', type=int, default=config.SEED)
    p.add_argument('--runs', type=int)
    p.add_argument('--nodes', type=int)
    p.add_argument('--time', type=float)
    p.add_argument('--variants', type=str,
                   help='comma-separated subset of config.VARIANTS')
    p.add_argument('--hloa-pop', type=int)
    p.add_argument('--hloa-iter', type=int)
    p.add_argument('--out', type=str, default='outputs')
    return p.parse_args()


def main():
    args = parse_args()
    n_nodes = args.nodes or (20 if args.quick else config.N_NODES)
    n_runs = args.runs or (2 if args.quick else config.N_RUNS)
    sim_time = args.time or (10.0 if args.quick else config.SIM_TIME)
    eval_times = [et for et in config.EVAL_TIMES if et <= sim_time] or [sim_time]
    variants = args.variants.split(',') if args.variants else list(config.VARIANTS)
    if args.hloa_pop:
        config.HLOA_POP = args.hloa_pop
    if args.hloa_iter:
        config.HLOA_MAX_ITER = args.hloa_iter

    logger.info(f"Nodes={n_nodes} | Time={sim_time}s | Runs={n_runs} | "
                f"Seed={args.seed} | HLOA={config.HLOA_POP}x{config.HLOA_MAX_ITER} | "
                f"Variants={variants}")

    master = np.random.default_rng(args.seed)
    run_seeds = [int(s) for s in master.choice(10000, n_runs, replace=False)]

    results = {v: [] for v in variants}
    router_fitness = []
    total = Timer()
    for run_idx, seed in enumerate(run_seeds):
        timer = Timer()
        train = ATMANETSimulation(n_nodes, sim_time, seed + config.TRAIN_SEED_OFFSET, 'full')
        train.bootstrap()
        weights, fit = train.train_router()
        router_fitness.append({'train_pdr': fit, 'hloa_iterations': train.hloa_iterations})
        for v in variants:
            sim = ATMANETSimulation(n_nodes, sim_time, seed, v)
            sim.bootstrap()
            sim.gnn.set_weights(weights)
            results[v].append(sim.run(eval_times))
        last = {v: results[v][-1][-1] for v in variants}
        logger.info(f"Run {run_idx + 1}/{n_runs} seed={seed} train-PDR={fit:.3f} (HLOA iters={train.hloa_iterations}) "
                    + " ".join(f"{v}:PDR={m['pdr']:.3f}" for v, m in last.items())
                    + f" ({timer.elapsed_str()})")

    aggregated = aggregate(results)
    print_results(aggregated)
    ref = 'full' if 'full' in variants else variants[0]
    paired = paired_vs(results, ref) if len(variants) > 1 else {}
    if args.ablation or paired:
        print_paired(paired, ref)

    os.makedirs(args.out, exist_ok=True)
    save_results({'meta': {'seeds': run_seeds, 'n_nodes': n_nodes, 'sim_time': sim_time,
                           'variants': variants, 'router_train_pdr': router_fitness,
                           'hloa': [config.HLOA_POP, config.HLOA_MAX_ITER]},
                  'aggregated': aggregated, 'paired_vs_' + ref: paired},
                 os.path.join(args.out, 'simulation_results.json'))
    if not args.no_plot:
        plot_results(aggregated, os.path.join(args.out, 'figures'))
    logger.info(f"Total time: {total.elapsed_str()}")


if __name__ == '__main__':
    main()
