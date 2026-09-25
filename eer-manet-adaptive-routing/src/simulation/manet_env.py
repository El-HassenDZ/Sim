"""
simulation/manet_env.py
=======================
AT-AEES-MANET Simulation Environment (corrected)

Abstract discrete-time simulator (1 s steps). NOT a packet-level network
simulator: there is no MAC, no queue, no route discovery and no control
traffic. Consequences for the metrics:
- delay is a hop-count proxy (HOP_DELAY per hop), not a measured delay;
- throughput is bounded by the offered load N_FLOWS × DATA_RATE × PKT_SIZE;
- there is no routing-overhead metric.

Corrections with respect to the received version:
- One behaviour model (simulation/behaviour.py) drives both watchdog
  observations and data forwarding; on-off and collusion attackers now
  drop data packets (they never did in _route_packet).
- The trust manager no longer receives the attacker list.
- Forwarding is greedy geographic: a next hop must be closer to the
  destination (the previous greedy choice ignored the destination), and a
  packet is delivered only by a hop to the destination itself.
- Energy is charged per transmitted/received packet and per hop with the
  first-order radio model (E_TX, E_RX, E_AMP·d²), so routing affects
  energy. E_AMP was declared and never used; the previous drain charged
  every node DATA_RATE packets per second whatever it sent.
- Flows are fixed per run between honest nodes; flagged nodes still send
  (a false positive no longer removes its packets from the denominator).
- Bootstrap runs in [-BOOTSTRAP_TIME, 0): simulated time no longer goes
  back from 8 s to 0 s.
- The router (GNN weights) is trained by HLOA on an independent training
  scenario with a deterministic fitness (PDR only); it is no longer tuned
  on the test scenario with ground-truth detection terms.
"""

from typing import Dict, List, Optional, Tuple

import numpy as np

import config
from clustering.fcmvc import FCMVC
from gnn.at_efiagnn import ATEFIAGNN
from optimization.hloa import HLOA
from simulation.behaviour import AttackBehaviour, assign_attack_types
from trust.adaptive_trust import SlidingWindowTrustManager

VARIANT_FLAGS = {
    'full': {},
    'no_sliding_window': dict(sliding_window=False),
    'fixed_threshold': dict(adaptive_threshold=False),
    'no_onoff_detector': dict(onoff_detector=False),
    'no_collusion_filter': dict(collusion_filter=False),
    'no_defense': {},
    'no_attack': {},
}


class ATMANETSimulation:
    """One run of one variant on one scenario (seed)."""

    def __init__(self, n_nodes: int, sim_time: float, seed: int,
                 variant: str = 'full'):
        if variant not in VARIANT_FLAGS:
            raise ValueError(f"unknown variant {variant!r}")
        self.n_nodes = n_nodes
        self.sim_time = sim_time
        self.seed = seed
        self.variant = variant
        self.defense = variant != 'no_defense'
        self.attack = variant != 'no_attack'

        # Independent streams: identical scenario, mobility, flows and
        # behaviour draws for every variant of the same seed.
        s_scen, s_mob, s_beh, s_flow, s_fcm, s_fit, s_gnn = \
            np.random.SeedSequence(seed).spawn(7)
        scen = np.random.default_rng(s_scen)

        n_mal = max(1, int(n_nodes * config.MALICIOUS_RATIO))
        self.malicious_ids = sorted(int(m) for m in scen.permutation(n_nodes)[:n_mal])
        self.attack_map = assign_attack_types(self.malicious_ids, scen)

        self.positions = scen.uniform(0, config.AREA_SIZE, (n_nodes, 2))
        self.waypoints = scen.uniform(0, config.AREA_SIZE, (n_nodes, 2))
        self.speeds = scen.uniform(config.MIN_SPEED, config.MAX_SPEED, n_nodes)
        self.pause_timers = np.zeros(n_nodes)
        self.moving = np.ones(n_nodes, dtype=bool)
        self.energies = np.full(n_nodes, config.E_INITIAL)
        self.mob_rng = np.random.default_rng(s_mob)
        self.fcm_rng = np.random.default_rng(s_fcm)

        self.behaviour = AttackBehaviour(n_nodes, self.malicious_ids,
                                         self.attack_map, seed=s_beh,
                                         active=self.attack)
        self.trust_mgr = SlidingWindowTrustManager(
            n_nodes, recommendation_fn=self.behaviour.recommend,
            **VARIANT_FLAGS[variant])
        self.gnn = ATEFIAGNN(np.random.default_rng(s_gnn))

        mal = set(self.malicious_ids)
        self.honest_ids = [i for i in range(n_nodes) if i not in mal]
        flow_rng = np.random.default_rng(s_flow)
        self.flows = [tuple(int(x) for x in flow_rng.choice(self.honest_ids, 2, replace=False))
                      for _ in range(config.N_FLOWS)]

        fit_rng = np.random.default_rng(s_fit)
        self._fit_pairs = [tuple(int(x) for x in fit_rng.choice(self.honest_ids, 2, replace=False))
                           for _ in range(config.HLOA_FITNESS_PAIRS)]
        self._fit_draws = fit_rng.random((config.HLOA_FITNESS_PAIRS, n_nodes))

        self.cluster_membership = np.full((n_nodes, config.N_CLUSTERS),
                                          1.0 / config.N_CLUSTERS)
        self.ch_ids: List[int] = []
        self._refresh_geometry()

    # ── Geometry ──────────────────────────────────────────────

    def _refresh_geometry(self) -> None:
        diff = self.positions[:, None, :] - self.positions[None, :, :]
        self._dist = np.linalg.norm(diff, axis=2)
        alive = self.energies > 0
        adj = (self._dist < config.TX_RANGE) & alive[:, None] & alive[None, :]
        np.fill_diagonal(adj, False)
        self._adj = adj

    def _get_neighbours(self, node_id: int) -> List[int]:
        return [int(j) for j in np.nonzero(self._adj[node_id])[0]]

    def _move_nodes(self, dt: float) -> None:
        """Random waypoint."""
        for i in range(self.n_nodes):
            self.moving[i] = False
            if self.energies[i] <= 0:
                continue
            if self.pause_timers[i] > 0:
                self.pause_timers[i] -= dt
                continue
            direction = self.waypoints[i] - self.positions[i]
            dist = np.linalg.norm(direction)
            if dist < 1.0:
                self.waypoints[i] = self.mob_rng.uniform(0, config.AREA_SIZE, 2)
                self.pause_timers[i] = config.PAUSE_TIME
                self.speeds[i] = self.mob_rng.uniform(config.MIN_SPEED, config.MAX_SPEED)
            else:
                step = min(self.speeds[i] * dt, dist)
                self.positions[i] += (direction / dist) * step
                self.positions[i] = np.clip(self.positions[i], 0, config.AREA_SIZE)
                self.moving[i] = True

    # ── Trust ─────────────────────────────────────────────────

    def _watchdog(self, t: float) -> None:
        """Every node observes whether each neighbour forwards (promiscuous
        monitoring). Draws are consumed even without a defence so that all
        variants share the same random streams."""
        for obs in range(self.n_nodes):
            for tgt in np.nonzero(self._adj[obs])[0]:
                fwd = self.behaviour.forwards(int(tgt), t, stream='watch')
                if self.defense:
                    self.trust_mgr.record_observation(obs, int(tgt), t, fwd)

    def _trust_update(self, t: float) -> None:
        if not self.defense:
            return
        self.trust_mgr.update_all(t, self.positions)
        rt = self.trust_mgr.get_all_rt()
        flagged = self.trust_mgr.get_flagged()
        self.gnn.update_route_weights(
            float(rt.mean()),
            float(self.energies.mean() / config.E_INITIAL),
            len(flagged) / self.n_nodes)
        self._fit_clusters()

    def _fit_clusters(self) -> None:
        rt, st, su, flagged = self._trust_inputs()
        clusterer = FCMVC(config.N_CLUSTERS, self.fcm_rng)
        clusterer.fit(self.positions, self.energies, rt)
        self.cluster_membership = clusterer.membership
        # Cluster heads are computed for reporting only: routing does not
        # use them (it did not in the received version either).
        self.ch_ids = clusterer.select_cluster_heads(
            self.positions, self.energies, rt, st, su, flagged)

    def _trust_inputs(self):
        n = self.n_nodes
        if not self.defense:
            return np.full(n, 0.5), np.ones(n), np.zeros(n), set()
        tm = self.trust_mgr
        rt = tm.get_all_rt()
        st = np.array([tm.get_stability(i) for i in range(n)])
        su = np.array([tm.get_suspicion(i) for i in range(n)])
        return rt, st, su, tm.get_flagged()

    def bootstrap(self) -> None:
        """Fill N_WINDOWS windows before t = 0, as the original did."""
        rounds = config.WINDOW_SIZE * (config.N_WINDOWS + 1)
        for r in range(rounds):
            t = -config.BOOTSTRAP_TIME * (1.0 - r / rounds)
            self._watchdog(t)
            if (r + 1) % config.WINDOW_SIZE == 0 and self.defense:
                self.trust_mgr.update_all(t, self.positions)
        if self.defense:
            self.trust_mgr.update_all(-1e-3, self.positions)
        self._fit_clusters()

    # ── Routing ───────────────────────────────────────────────

    def _scores(self) -> Tuple[np.ndarray, set]:
        rt, st, su, flagged = self._trust_inputs()
        features = self._build_feature_matrix(rt, st, su)
        scores = self.gnn.compute_routing_scores(
            features, self._adj.astype(float), rt, st, self.energies, su)
        return scores, flagged

    def _build_feature_matrix(self, rt, st, su) -> np.ndarray:
        n = self.n_nodes
        f = np.zeros((n, config.GNN_INPUT_DIM))
        f[:, 0] = self.positions[:, 0] / config.AREA_SIZE
        f[:, 1] = self.positions[:, 1] / config.AREA_SIZE
        f[:, 2] = self.energies / config.E_INITIAL
        f[:, 3] = rt
        f[:, 4] = st
        f[:, 5] = self.cluster_membership.max(axis=1)
        f[:, 6] = np.minimum(1.0, self._adj.sum(axis=1) / 10.0)
        f[:, 7] = su
        f[:, 8] = np.where(self.moving, self.speeds, 0.0) / config.MAX_SPEED
        return f

    def _transmit(self, i: int, j: int) -> float:
        bits = config.PKT_SIZE * 8
        e_tx = config.E_TX * bits + config.E_AMP * bits * self._dist[i, j] ** 2
        e_rx = config.E_RX * bits
        used = min(e_tx, self.energies[i]) + min(e_rx, self.energies[j])
        self.energies[i] = max(0.0, self.energies[i] - e_tx)
        self.energies[j] = max(0.0, self.energies[j] - e_rx)
        return used

    def _route_packet(self, src: int, dst: int, scores: np.ndarray,
                      flagged: set, t: float, draws: Optional[np.ndarray] = None,
                      account: bool = True) -> Tuple[bool, int, float]:
        """
        Greedy geographic forwarding with trust-aware tie-breaking: among
        the neighbours strictly closer to dst (not visited, alive, not
        flagged), pick the highest composite score. No perimeter mode:
        a local maximum drops the packet. Returns (delivered, hops, energy).

        draws: optional per-node uniform draws (training fitness, so that
        every candidate weight vector is judged on the same outcomes).
        """
        current, hops, energy = src, 0, 0.0
        visited = {src}
        while hops < config.MAX_HOPS:
            if self.energies[current] <= 0:
                return False, hops, energy
            if self._adj[current, dst]:
                if account:
                    energy += self._transmit(current, dst)
                return True, hops + 1, energy
            d_cur = self._dist[current, dst]
            cand = [int(j) for j in np.nonzero(self._adj[current])[0]
                    if j not in visited and j not in flagged
                    and self._dist[j, dst] < d_cur]
            if not cand:
                return False, hops, energy
            nb = max(cand, key=lambda j: scores[j])
            if account:
                energy += self._transmit(current, nb)
            hops += 1
            if draws is None:
                ok = self.behaviour.forwards(nb, t, stream='data')
            else:
                ok = self.behaviour.decide(nb, t, draws[nb])
            if not ok:
                return False, hops, energy
            visited.add(nb)
            current = nb
        return False, hops, energy

    # ── Router training ───────────────────────────────────────

    def fitness(self, weights: np.ndarray) -> float:
        """Deterministic PDR over fixed (src, dst) pairs and fixed draws."""
        self.gnn.set_weights(weights)
        scores, flagged = self._scores()
        ok = 0
        for k, (src, dst) in enumerate(self._fit_pairs):
            delivered, _, _ = self._route_packet(
                src, dst, scores, flagged, 0.0,
                draws=self._fit_draws[k], account=False)
            ok += delivered
        return ok / len(self._fit_pairs)

    def train_router(self, verbose: bool = False) -> Tuple[np.ndarray, float]:
        hloa = HLOA(self.gnn.weight_dim, self.fitness,
                    np.random.default_rng([self.seed, 1]), target_fitness=1.0)
        best_w, best_fit = hloa.optimise(verbose=verbose)
        self.gnn.set_weights(best_w)
        self.hloa_iterations = hloa.iterations_run
        return best_w, best_fit

    # ── Main run ──────────────────────────────────────────────

    def run(self, eval_times: List[float], verbose: bool = False) -> List[Dict]:
        results = []
        dt = config.TIME_STEP
        sent = recv = 0
        total_delay = total_hops = energy_used = 0.0
        t = 0.0
        while t <= self.sim_time + 1e-9:
            self._move_nodes(dt)
            self._refresh_geometry()
            self._watchdog(t)
            k = t / config.TRUST_UPDATE_INTERVAL
            if abs(k - round(k)) < 1e-9:
                self._trust_update(t)
            scores, flagged = self._scores()
            for src, dst in self.flows:
                if self.energies[src] <= 0:
                    continue              # dead source generates nothing
                for _ in range(config.DATA_RATE):
                    sent += 1
                    ok, hops, e = self._route_packet(src, dst, scores, flagged, t)
                    energy_used += e
                    if ok:
                        recv += 1
                        total_hops += hops
                        total_delay += hops * config.HOP_DELAY
                self._refresh_geometry()  # nodes may have died
            if any(abs(t - et) < 1e-9 for et in eval_times):
                m = self._compute_metrics(t + dt, sent, recv, total_delay,
                                          total_hops, energy_used, t)
                results.append(m)
                if verbose:
                    print(f"  [{self.variant}] t={t:5.1f}s | PDR={m['pdr']:.3f} | "
                          f"DR={m['detection_rate']:.1f}% | FPR={m['false_positive_rate']:.1f}%")
            t += dt
        return results

    def _compute_metrics(self, elapsed: float, sent: int, recv: int,
                         total_delay: float, total_hops: float,
                         energy_used: float, t: float) -> Dict:
        bits = config.PKT_SIZE * 8
        flagged = self.trust_mgr.get_flagged() if self.defense else set()
        malicious = set(self.malicious_ids) if self.attack else set()
        honest = set(range(self.n_nodes)) - malicious
        nan = float('nan')

        tp = len(flagged & malicious)
        fp = len(flagged & honest)
        per_attack = {}
        for at in config.ATTACK_TYPES:
            nodes = {m for m in malicious if self.attack_map[m] == at}
            per_attack[at] = (100.0 * len(nodes & flagged) / len(nodes)) if nodes else nan

        return {
            'time': t,
            'pdr': recv / sent if sent else nan,
            'throughput_kbps': recv * bits / elapsed / 1000.0,
            'delay_proxy_ms': 1000.0 * total_delay / recv if recv else nan,
            'avg_hops': total_hops / recv if recv else nan,
            'energy_J': energy_used,
            'energy_eff_kbit_per_J': (recv * bits / 1000.0) / energy_used if energy_used else nan,
            'alive_nodes': float((self.energies > 0).sum()),
            'detection_rate': 100.0 * tp / len(malicious) if malicious else nan,
            'false_positive_rate': 100.0 * fp / len(honest) if honest else nan,
            'precision': 100.0 * tp / (tp + fp) if (tp + fp) else nan,
            'adaptive_threshold': (self.trust_mgr.adaptive_threshold
                                   if self.defense else nan),
            'per_attack_dr': per_attack,
            'route_alpha': self.gnn.route_alpha,
            'route_beta': self.gnn.route_beta,
            'route_gamma': self.gnn.route_gamma,
        }
