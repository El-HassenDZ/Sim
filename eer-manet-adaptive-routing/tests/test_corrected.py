import inspect
import os
import sys

import numpy as np
import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'src'))
sys.path.insert(0, ROOT)

import config                                                      # noqa: E402
from gnn.at_efiagnn import ATEFIAGNN, ATEFIAGNNLayer               # noqa: E402
from simulation.behaviour import AttackBehaviour, assign_attack_types  # noqa: E402
from simulation.manet_env import ATMANETSimulation                 # noqa: E402
from trust.adaptive_trust import SlidingWindowTrustManager         # noqa: E402
from evaluation.metrics import mean_ci95                           # noqa: E402


def loop_forward(layer, H, adj, tw):
    """Per-node loops of the received version, kept as the reference."""
    n = H.shape[0]
    out = np.zeros((n, layer.W_self.shape[1]))
    for i in range(n):
        h_self = H[i] @ layer.W_self
        nb = np.where(adj[i] > 0)[0]
        if len(nb):
            w = tw[i, nb]
            ws = w.sum() + 1e-10
            agg = sum((wj / ws) * H[j] for j, wj in zip(nb, w))
            inter = sum((wj / ws) * (H[i] * H[j]) for j, wj in zip(nb, w))
            h_n, h_i = agg @ layer.W_neigh, inter @ layer.W_inter
        else:
            h_n = np.zeros(layer.W_neigh.shape[1])
            h_i = np.zeros(layer.W_inter.shape[1])
        out[i] = np.maximum(0.0, h_self + h_n + h_i + layer.bias)
    return out


def test_gnn_vectorised_matches_loops():
    rng = np.random.default_rng(0)
    layer = ATEFIAGNNLayer(9, 16, rng)
    H = rng.random((25, 9))
    adj = (rng.random((25, 25)) < 0.2).astype(float)
    np.fill_diagonal(adj, 0)
    adj[3] = 0                                  # isolated node
    tw = rng.random((25, 25))
    assert np.allclose(layer.forward(H, adj, tw), loop_forward(layer, H, adj, tw))


def test_trust_manager_has_no_ground_truth():
    params = inspect.signature(SlidingWindowTrustManager.__init__).parameters
    assert 'malicious_ids' not in params and 'attack_map' not in params


def test_attack_types_proportional():
    amap = assign_attack_types(list(range(10)), np.random.default_rng(0))
    counts = {a: list(amap.values()).count(a) for a in config.ATTACK_TYPES}
    assert counts == {'blackhole': 3, 'grayhole': 3, 'on_off': 2, 'collusion': 2} \
        or counts == {'blackhole': 3, 'grayhole': 2, 'on_off': 3, 'collusion': 2}
    assert sum(counts.values()) == 10


def test_onoff_drops_data_in_bad_phase():
    beh = AttackBehaviour(1, [0], {0: 'on_off'}, seed=0)
    phase = beh._phase[0]
    bad_t = (config.ONOFF_DUTY * config.ONOFF_PERIOD + 1.0 - phase) % config.ONOFF_PERIOD
    assert not any(beh.forwards(0, bad_t, 'data') for _ in range(50))


def test_collusion_drops_data():
    beh = AttackBehaviour(1, [0], {0: 'collusion'}, seed=0)
    rate = np.mean([beh.forwards(0, 0.0) for _ in range(4000)])
    assert rate == pytest.approx(1 - config.COLLUSION_DROP, abs=0.03)


def test_no_attack_variant_is_honest():
    beh = AttackBehaviour(1, [0], {0: 'blackhole'}, seed=0, active=False)
    assert np.mean([beh.forwards(0, 0.0) for _ in range(2000)]) > 0.9


def test_routing_needs_progress_and_real_delivery():
    sim = ATMANETSimulation(3, 5.0, 0, 'no_defense')
    sim.positions = np.array([[0.0, 0.0], [200.0, 0.0], [400.0, 0.0]])
    sim.energies[:] = config.E_INITIAL
    sim._refresh_geometry()
    sim.behaviour = AttackBehaviour(3, [], {}, seed=0, active=False)
    draws = np.ones(3)                              # every relay forwards
    ok, hops, _ = sim._route_packet(0, 2, np.zeros(3), set(), 0.0, draws=draws,
                                    account=False)
    assert ok and hops == 2
    # relay behind the source is never used
    sim.positions = np.array([[200.0, 0.0], [0.0, 0.0], [600.0, 0.0]])
    sim._refresh_geometry()
    ok, _, _ = sim._route_packet(0, 2, np.array([0, 10.0, 0]), set(), 0.0,
                                 draws=draws, account=False)
    assert not ok


def test_energy_depends_on_traffic():
    sim = ATMANETSimulation(5, 5.0, 0, 'no_defense')
    sim.positions = np.array([[0.0, 0.0], [100.0, 0.0], [900.0, 900.0],
                              [900.0, 0.0], [0.0, 900.0]])
    sim._refresh_geometry()
    e0 = sim.energies.copy()
    used = sim._transmit(0, 1)
    bits = config.PKT_SIZE * 8
    assert used == pytest.approx(config.E_TX * bits + config.E_AMP * bits * 1e4
                                 + config.E_RX * bits)
    assert e0.sum() - sim.energies.sum() == pytest.approx(used)


def test_fitness_deterministic():
    sim = ATMANETSimulation(20, 5.0, 3, 'full')
    sim.bootstrap()
    w = np.random.default_rng(1).uniform(-1, 1, sim.gnn.weight_dim)
    assert sim.fitness(w) == sim.fitness(w)


def test_run_reproducible_and_bootstrap_before_zero():
    a = ATMANETSimulation(20, 5.0, 7, 'full'); a.bootstrap()
    b = ATMANETSimulation(20, 5.0, 7, 'full'); b.bootstrap()
    assert a.trust_mgr._prev_t < 0
    ra, rb = a.run([5.0]), b.run([5.0])
    assert ra[0]['pdr'] == rb[0]['pdr'] and ra[0]['energy_J'] == rb[0]['energy_J']


def test_per_attack_nan_when_absent():
    sim = ATMANETSimulation(20, 2.0, 7, 'full')
    sim.bootstrap()
    m = sim.run([2.0])[0]
    absent = [a for a in config.ATTACK_TYPES if a not in sim.attack_map.values()]
    assert absent and all(np.isnan(m['per_attack_dr'][a]) for a in absent)


def test_mean_ci95_ignores_nan():
    s = mean_ci95([1.0, float('nan'), 3.0])
    assert s['n'] == 2 and s['mean'] == 2.0
