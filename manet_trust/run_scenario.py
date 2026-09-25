"""
Abstract one-hop forwarding scenario comparing: no defence, original
manager, corrected manager. There is NO routing protocol here (no AODV,
no ns-3): an observer hands a packet to a random one-hop neighbour and
observes whether it is forwarded. 'fwd_success' is therefore a one-hop
forwarding success ratio, not an end-to-end PDR.

    python3 run_scenario.py [n_seeds]
"""
import importlib.util
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from trust.adaptive_trust import SlidingWindowTrustManager          # noqa: E402
from trust.behaviour import AttackBehaviour, ATTACKS                # noqa: E402
from trust.evaluation import detection_metrics, mean_ci95           # noqa: E402

N, AREA, T, WARMUP = 30, 600.0, 200, 50
PER_STEP, SPEED_SD, PROBE_RATE = 3, 5.0, 0.1


def _load_original():
    path = os.path.join(HERE, 'original', 'trust', 'adaptive_trust.py')
    spec = importlib.util.spec_from_file_location('orig_adaptive_trust', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.SlidingWindowTrustManager


def _setup(seed):
    mal = list(range(2 * len(ATTACKS)))
    amap = {m: ATTACKS[i % len(ATTACKS)] for i, m in enumerate(mal)}
    return mal, amap


def _mobility(seed):
    rng = np.random.default_rng([seed, 1])       # independent of behaviour
    pos = rng.uniform(0, AREA, (N, 2))
    for t in range(T):
        pos = np.clip(pos + rng.normal(0, SPEED_SD, (N, 2)), 0, AREA)
        yield t, pos.copy()


def run(seed, mode):
    mal, amap = _setup(seed)
    beh = AttackBehaviour(N, mal, amap, seed=seed)
    sel = np.random.default_rng([seed, 2])        # next-hop choice
    if mode == 'corrected':
        tm = SlidingWindowTrustManager(N, recommendation_fn=beh.recommend)
    elif mode == 'original':
        tm = _load_original()(N, np.random.default_rng(seed), mal, amap)
    else:
        tm = None
    tprs, fprs, ok, tries = [], [], 0, 0
    energies = np.full(N, 100.0)
    for t, pos in _mobility(seed):
        flagged = tm.get_flagged() if tm is not None else set()
        for obs in range(N):
            d = np.linalg.norm(pos - pos[obs], axis=1)
            nb = [j for j in np.nonzero(d <= 250.0)[0] if j != obs]
            good = [j for j in nb if j not in flagged]
            for _ in range(PER_STEP):
                pool = good if (good and sel.random() >= PROBE_RATE) else nb
                if not pool:
                    continue
                j = int(sel.choice(pool))
                if mode == 'original':
                    tm.record_interaction(obs, j, float(t))
                    continue
                fwd = beh.forwards(j, float(t))
                if t >= WARMUP:
                    ok += fwd
                    tries += 1
                if tm is not None:
                    tm.record_observation(obs, j, float(t), fwd)
        if tm is not None:
            tm.update_all(float(t), pos, energies)
        if t >= WARMUP:
            m = detection_metrics(tm.get_flagged() if tm else set(), mal, N)
            tprs.append(m['tpr'])
            fprs.append(m['fpr'])
    return dict(tpr=np.mean(tprs) if tprs else np.nan,
                fpr=np.mean(fprs) if fprs else np.nan,
                fwd=ok / tries if tries else np.nan)


if __name__ == '__main__':
    seeds = range(int(sys.argv[1]) if len(sys.argv) > 1 else 10)
    for mode in ('none', 'original', 'corrected'):
        res = [run(s, mode) for s in seeds]
        line = [mode]
        for k in ('tpr', 'fpr', 'fwd'):
            ci = mean_ci95([r[k] for r in res])
            line.append(f"{k}={ci['mean']:.3f}±{ci['half_width']:.3f}")
        print('  '.join(line), f"(n={len(res)})")
