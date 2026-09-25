import importlib.util
import inspect
import os
import sys

import numpy as np
import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

import config                                                   # noqa: E402
from trust.adaptive_trust import SlidingWindowTrustManager       # noqa: E402
from trust.behaviour import AttackBehaviour                      # noqa: E402
from trust.evaluation import detection_metrics, mean_ci95        # noqa: E402


def load_original():
    path = os.path.join(ROOT, 'original', 'trust', 'adaptive_trust.py')
    spec = importlib.util.spec_from_file_location('orig_at', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.SlidingWindowTrustManager


def star(n):
    """Node 0 at centre, everyone within range of everyone."""
    return np.array([[i * 10.0, 0.0] for i in range(n)])


def test_manager_has_no_ground_truth():
    params = inspect.signature(SlidingWindowTrustManager.__init__).parameters
    assert 'malicious_ids' not in params and 'attack_map' not in params
    assert not hasattr(SlidingWindowTrustManager, '_simulate_behaviour')


def test_original_idt_uses_neighbours_own_trust():
    """Documents the original defect: a blackhole's IDT is its neighbours' DT."""
    Orig = load_original()
    n = 6
    tm = Orig(n, np.random.default_rng(0), [0], {0: 'blackhole'})
    for t in range(5):
        for obs in range(1, n):
            for tgt in range(n):
                if tgt != obs:
                    tm.record_interaction(obs, tgt, float(t))
        tm.update_all(float(t), star(n), np.full(n, 100.0))
    s = tm.states[0]
    assert s.dt == 0.0
    assert s.idt > 0.8                         # should be ~0
    assert s.rt == pytest.approx(config.TRUST_BETA * s.idt, abs=1e-9)


def test_blackhole_idt_reflects_reports_about_it():
    n = 6
    tm = SlidingWindowTrustManager(n)
    for t in range(5):
        for obs in range(1, n):
            for tgt in range(n):
                if tgt != obs:
                    tm.record_observation(obs, tgt, float(t), forwarded=(tgt != 0))
        tm.update_all(float(t), star(n))
    s = tm.states[0]
    # Beta prior: 5 drops -> (0+1)/(5+2) = 0.143
    assert s.idt < 0.2 and s.rt < 0.2
    assert 0 in tm.get_flagged()
    assert all(i not in tm.get_flagged() for i in range(1, n))


def test_observer_matters():
    tm = SlidingWindowTrustManager(3)
    for _ in range(10):
        tm.record_observation(0, 2, 0.0, True)
        tm.record_observation(1, 2, 0.0, False)
    tm.update_all(0.0, np.array([[0, 0], [1000, 0], [2000, 0]], float))
    assert tm.get_trust_of(0, 2) > 0.8 and tm.get_trust_of(1, 2) < 0.2


def test_partial_window_not_flushed_and_beta_prior():
    tm = SlidingWindowTrustManager(2)
    tm.record_observation(0, 1, 0.0, True)
    tm.update_all(0.0, star(2))
    assert len(tm.evidence[(0, 1)].windows) == 0
    assert tm.DT[0, 1] == pytest.approx(2 / 3)   # original: 1.0


def test_evidence_recorded_while_isolated():
    tm = SlidingWindowTrustManager(2)
    tm.states[1].isolated = True
    tm.states[1].isolation_until = 100.0
    tm.record_observation(0, 1, 1.0, True)
    assert tm.evidence[(0, 1)].total_n == 1


def test_mad_filter_resists_colluding_badmouthers():
    """
    4 honest recommenders vs 3 colluders that forward correctly (so they
    pass the recommender-trust gate) but badmouth honest node 1.
    """
    n = 9
    coll = [6, 7, 8]
    beh = AttackBehaviour(n, coll, {c: 'collusion' for c in coll}, seed=0)
    tm = SlidingWindowTrustManager(n, recommendation_fn=beh.recommend)
    for t in range(20):
        for obs in range(n):
            for tgt in range(n):
                if obs != tgt:
                    tm.record_observation(obs, tgt, float(t), True)
        tm.update_all(float(t), star(n))
    # observer 0 about honest node 1: colluders' 0.0 reports are filtered
    assert tm.IDT[0, 1] > 0.9
    # and without false reports the result is the same
    tm2 = SlidingWindowTrustManager(n)
    for t in range(20):
        for obs in range(n):
            for tgt in range(n):
                if obs != tgt:
                    tm2.record_observation(obs, tgt, float(t), True)
        tm2.update_all(float(t), star(n))
    assert tm.IDT[0, 1] == pytest.approx(tm2.IDT[0, 1])


def test_behaviour_streams_independent_per_node():
    a = AttackBehaviour(4, [], {}, seed=7)
    b = AttackBehaviour(4, [], {}, seed=7)
    for _ in range(50):
        b.forwards(0, 0.0)                      # extra draws on node 0 only
    seq_a = [a.forwards(1, 0.0) for _ in range(100)]
    seq_b = [b.forwards(1, 0.0) for _ in range(100)]
    assert seq_a == seq_b


def test_unknown_attack_rejected():
    with pytest.raises(ValueError):
        AttackBehaviour(2, [0], {0: 'wormhole'}, seed=0)


def test_flag_all_gives_fpr_one():
    m = detection_metrics(range(10), [0, 1], 10)
    assert m['tpr'] == 1.0 and m['fpr'] == 1.0


def test_mean_ci95():
    ci = mean_ci95([1.0, 2.0, 3.0])
    assert ci['mean'] == 2.0
    assert ci['half_width'] == pytest.approx(4.303 * 1.0 / np.sqrt(3))
