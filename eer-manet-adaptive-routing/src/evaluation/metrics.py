"""
evaluation/metrics.py
=====================
Aggregation across runs, confidence intervals and paired comparisons
between variants run in the same simulator on the same seeds.

Removed from the received version:
- BASELINE_DATA / compute_improvements: literature values from other
  simulators, in other units, compared as percentages (the quick run
  printed "+11174 %" energy efficiency and "-141738 %" energy);
- run_ttest: a "paired" t-test against baseline samples synthesised as
  N(value, 5 %·value) with a fixed seed. Its p-values measure nothing.
"""

import math
from typing import Dict, List

import numpy as np
from scipy import stats

import config

METRICS = ['pdr', 'throughput_kbps', 'delay_proxy_ms', 'avg_hops', 'energy_J',
           'energy_eff_kbit_per_J', 'alive_nodes', 'detection_rate',
           'false_positive_rate', 'precision', 'adaptive_threshold']


def mean_ci95(values) -> Dict:
    """NaN-aware mean, sample SD and 95 % Student-t half-width."""
    x = np.asarray([v for v in values if not (isinstance(v, float) and math.isnan(v))],
                   dtype=float)
    n = int(x.size)
    out = {'n': n, 'all': [float(v) for v in values]}
    if n == 0:
        out.update(mean=float('nan'), sd=float('nan'), ci95=float('nan'))
        return out
    out['mean'] = float(x.mean())
    if n < 2:
        out.update(sd=float('nan'), ci95=float('nan'))
        return out
    sd = float(x.std(ddof=1))
    out.update(sd=sd, ci95=float(stats.t.ppf(0.975, n - 1) * sd / math.sqrt(n)))
    return out


def aggregate(results: Dict[str, List[List[Dict]]]) -> Dict:
    """results[variant][run] = list of snapshots -> stats per variant/time."""
    out = {}
    for variant, runs in results.items():
        out[variant] = {}
        times = [s['time'] for s in runs[0]]
        for i, t in enumerate(times):
            snaps = [run[i] for run in runs]
            agg = {m: mean_ci95([s[m] for s in snaps]) for m in METRICS}
            agg['per_attack_dr'] = {
                at: mean_ci95([s['per_attack_dr'][at] for s in snaps])
                for at in config.ATTACK_TYPES}
            out[variant][t] = agg
    return out


def paired_vs(results: Dict[str, List[List[Dict]]], ref: str,
              metrics=('pdr', 'detection_rate', 'false_positive_rate',
                       'energy_J', 'delay_proxy_ms')) -> Dict:
    """
    Variant − ref at the last snapshot, paired by seed (same scenario,
    mobility, flows and behaviour draws). Paired t-test over runs.
    """
    out = {}
    ref_runs = results[ref]
    for variant, runs in results.items():
        if variant == ref:
            continue
        out[variant] = {}
        for m in metrics:
            a = np.array([r[-1][m] for r in runs], dtype=float)
            b = np.array([r[-1][m] for r in ref_runs], dtype=float)
            ok = ~(np.isnan(a) | np.isnan(b))
            d = a[ok] - b[ok]
            res = mean_ci95(list(d))
            if d.size >= 2 and np.std(d) > 0:
                res['p'] = float(stats.ttest_rel(a[ok], b[ok]).pvalue)
            else:
                res['p'] = float('nan')
            out[variant][m] = res
    return out


def _fmt(s: Dict, digits: int = 2) -> str:
    if s['n'] == 0 or math.isnan(s['mean']):
        return 'n/a'
    if math.isnan(s['ci95']):
        return f"{s['mean']:.{digits}f}"
    return f"{s['mean']:.{digits}f} ± {s['ci95']:.{digits}f}"


def print_results(aggregated: Dict) -> None:
    cols = [('pdr', 'PDR', 3), ('throughput_kbps', 'TP (kbps)', 1),
            ('delay_proxy_ms', 'Delay* (ms)', 2), ('energy_J', 'Energy (J)', 2),
            ('detection_rate', 'DR (%)', 1), ('false_positive_rate', 'FPR (%)', 1),
            ('precision', 'Prec. (%)', 1)]
    print(f"\n{'=' * 118}")
    print("  Results — mean ± 95 % CI half-width over runs "
          "(* delay = hop-count proxy, not a measured delay)")
    print(f"{'=' * 118}")
    header = f"  {'variant':<20} {'t':>4} | " + " | ".join(f"{c[1]:>16}" for c in cols)
    print(header)
    print("  " + "-" * (len(header) - 2))
    for variant, by_t in aggregated.items():
        for t, agg in by_t.items():
            cells = " | ".join(f"{_fmt(agg[m], d):>16}" for m, _, d in cols)
            print(f"  {variant:<20} {int(t):>4} | {cells}")

    print(f"\n  Per-attack detection rate (%) at the last snapshot "
          f"(n = runs in which the attack type is present)")
    for variant, by_t in aggregated.items():
        last = by_t[max(by_t)]
        parts = [f"{at}: {_fmt(s, 1)} (n={s['n']})"
                 for at, s in last['per_attack_dr'].items()]
        print(f"  {variant:<20} " + " | ".join(parts))


def print_paired(paired: Dict, ref: str) -> None:
    print(f"\n{'=' * 100}")
    print(f"  Paired differences vs '{ref}' at the last snapshot "
          f"(mean ± 95 % CI, paired t-test over seeds)")
    print(f"{'=' * 100}")
    for variant, by_m in paired.items():
        parts = []
        for m, s in by_m.items():
            p = 'n/a' if math.isnan(s['p']) else f"{s['p']:.3g}"
            parts.append(f"{m}: {_fmt(s, 3)} (p={p})")
        print(f"  {variant:<20} " + " | ".join(parts))
