"""
simulation/manet_env.py
=======================
AT-AEES-MANET Simulation Environment

Enhancements over base paper:
- 4 attack types simulated (blackhole, grayhole, on-off, collusion)
- Adaptive routing metric weights
- Per-attack detection tracking
- False positive rate computation
- Trust stability fed into routing decisions
"""

import numpy as np
from typing import Dict, List, Tuple, Optional
import config
from trust.adaptive_trust import SlidingWindowTrustManager
from clustering.fcmvc import FCMVC
from gnn.at_efiagnn import ATEFIAGNN
from optimization.hloa import HLOA


class ATMANETSimulation:
    """Full AT-AEES-MANET simulation."""

    def __init__(self, n_nodes: int, sim_time: float,
                 rng: np.random.Generator, seed: int):
        self.n_nodes  = n_nodes
        self.sim_time = sim_time
        self.rng      = rng
        self.seed     = seed

        # Assign malicious nodes
        n_mal = max(1, int(n_nodes * config.MALICIOUS_RATIO))
        all_ids = list(range(n_nodes))
        self.rng.shuffle(all_ids)
        self.malicious_ids = all_ids[:n_mal]

        # Assign attack types to malicious nodes
        self.attack_map: Dict[int, str] = {}
        attack_types = list(config.ATTACK_TYPES.keys())
        attack_probs = list(config.ATTACK_TYPES.values())
        for mid in self.malicious_ids:
            at = self.rng.choice(attack_types, p=attack_probs)
            self.attack_map[mid] = at

        # Node state
        self.positions = self.rng.uniform(0, config.AREA_SIZE, (n_nodes, 2))
        self.energies  = np.full(n_nodes, config.E_INITIAL)
        self.speeds    = self.rng.uniform(config.MIN_SPEED, config.MAX_SPEED, n_nodes)
        self.waypoints = self.rng.uniform(0, config.AREA_SIZE, (n_nodes, 2))
        self.pause_timers = np.zeros(n_nodes)

        # Trust manager
        self.trust_mgr = SlidingWindowTrustManager(
            n_nodes, rng, self.malicious_ids, self.attack_map)

        # GNN
        self.gnn = ATEFIAGNN(rng)

        # Metrics accumulator
        self.metrics_log: List[Dict] = []

        # Per-attack detection tracking
        self.per_attack_detected = {at: 0 for at in config.ATTACK_TYPES}
        self.per_attack_total    = {at: 0 for at in config.ATTACK_TYPES}
        for mid in self.malicious_ids:
            at = self.attack_map[mid]
            self.per_attack_total[at] += 1

    # ── Main run ──────────────────────────────────────────────

    def initialise(self, verbose: bool = True) -> None:
        """Run trust + clustering + HLOA optimisation."""
        if verbose:
            print(f"\n─── Initialising AT-AEES-MANET ───")

        # Step 1: Bootstrap trust — fill N_WINDOWS complete sliding windows
        # Each node systematically observes all neighbours enough times
        # to populate the full window history before simulation starts.
        if verbose:
            print("  Step 1: Bootstrapping trust (sliding-window DT/IDT/RT)...")

        total_rounds = config.WINDOW_SIZE * (config.N_WINDOWS + 1)
        for round_idx in range(total_rounds):
            t_boot = (round_idx / total_rounds) * 8.0
            for obs in range(self.n_nodes):
                for tgt in self._get_neighbours(obs):
                    self.trust_mgr.record_interaction(obs, tgt, t_boot)
            # Flush completed windows every WINDOW_SIZE rounds
            if (round_idx + 1) % config.WINDOW_SIZE == 0:
                self.trust_mgr.update_all(t_boot, self.positions, self.energies)

        self.trust_mgr.update_all(8.0, self.positions, self.energies)

        flagged = self.trust_mgr.get_flagged()
        rt_vals  = self.trust_mgr.get_all_rt()
        avg_trust = float(rt_vals.mean())
        if verbose:
            print(f"    Avg RT: {avg_trust:.3f} | "
                  f"Flagged: {len(flagged)} | "
                  f"Threshold: {self.trust_mgr.adaptive_threshold:.3f}")

        # Step 2: Clustering
        if verbose:
            print("  Step 2: FCMVC clustering...")
        clusterer = FCMVC(config.N_CLUSTERS, self.rng)
        trusted_mask = np.array([i not in flagged for i in range(self.n_nodes)])
        clusterer.fit(self.positions, self.energies, rt_vals, trusted_mask)

        stability  = np.array([self.trust_mgr.get_stability(i)
                                for i in range(self.n_nodes)])
        suspicion  = np.array([self.trust_mgr.get_suspicion(i)
                                for i in range(self.n_nodes)])
        self.ch_ids = clusterer.select_cluster_heads(
            self.positions, self.energies, rt_vals,
            stability, suspicion, flagged)
        self.cluster_labels    = clusterer.labels
        self.cluster_membership = clusterer.membership

        if verbose:
            print(f"    Clusters: {config.N_CLUSTERS} | CHs: {self.ch_ids}")

        # Step 3: Build GNN
        if verbose:
            print(f"  Step 3: Building AT-EFIAGNN (dim={self.gnn.weight_dim})...")

        # Step 4: HLOA optimisation
        if verbose:
            print("  Step 4: HLOA optimisation...")
            print(f"\n[HLOA] dim={self.gnn.weight_dim}, "
                  f"pop={config.HLOA_POP}, iter={config.HLOA_MAX_ITER}")

        def fitness_fn(weights: np.ndarray) -> float:
            return self._evaluate_weights(weights)

        hloa = HLOA(self.gnn.weight_dim, fitness_fn, self.rng)
        best_w, best_fit = hloa.optimise(verbose=verbose)
        self.gnn.set_weights(best_w)

        if verbose:
            print(f"[HLOA] Best fitness: {best_fit:.6f}")

    def run(self, eval_times: List[float],
            verbose: bool = True) -> List[Dict]:
        """Run full simulation and return metrics at each eval time."""
        results = []
        dt_step = 1.0  # 1 second timesteps
        t = 0.0
        pkt_sent = 0
        pkt_recv = 0
        total_delay = 0.0
        total_energy_start = self.energies.sum()

        while t <= self.sim_time:
            # Move nodes
            self._move_nodes(dt_step)
            # Drain energy
            self._drain_energy(dt_step)
            # Record interactions
            for obs in range(self.n_nodes):
                for tgt in self._get_neighbours(obs):
                    self.trust_mgr.record_interaction(obs, tgt, t)
            # Update trust every 5 seconds
            if int(t) % 5 == 0:
                self.trust_mgr.update_all(t, self.positions, self.energies)
                # Update GNN routing weights
                rt_vals = self.trust_mgr.get_all_rt()
                flagged = self.trust_mgr.get_flagged()
                attack_rate = len(flagged) / self.n_nodes
                self.gnn.update_route_weights(
                    float(rt_vals.mean()),
                    float(self.energies.mean() / config.E_INITIAL),
                    attack_rate
                )

            # Simulate packet transmission
            sent, recv, delay = self._simulate_traffic(t)
            pkt_sent += sent
            pkt_recv += recv
            total_delay += delay

            # Snapshot at eval times
            for et in eval_times:
                if abs(t - et) < 0.5 and not any(
                        abs(r['time'] - et) < 0.5 for r in results):
                    m = self._compute_metrics(
                        t, pkt_sent, pkt_recv, total_delay,
                        total_energy_start)
                    results.append(m)
                    if verbose:
                        print(f"  t={t:5.1f}s | PDR={m['pdr']:.3f} | "
                              f"TP={m['throughput_kbps']:.1f} kbps | "
                              f"EC={m['energy_mJ']:.1f}mJ | "
                              f"DR={m['detection_rate']:.1f}% | "
                              f"Thresh={m['adaptive_threshold']:.3f}")
            t += dt_step

        return results

    # ── Internal helpers ──────────────────────────────────────

    def _get_neighbours(self, node_id: int) -> List[int]:
        dists = np.linalg.norm(self.positions - self.positions[node_id], axis=1)
        alive = self.energies > 0
        return [j for j in np.where((dists < config.TX_RANGE) & alive)[0]
                if j != node_id]

    def _move_nodes(self, dt: float) -> None:
        for i in range(self.n_nodes):
            if self.energies[i] <= 0:
                continue
            if self.pause_timers[i] > 0:
                self.pause_timers[i] -= dt
                continue
            direction = self.waypoints[i] - self.positions[i]
            dist = np.linalg.norm(direction)
            if dist < 1.0:
                self.waypoints[i] = self.rng.uniform(
                    0, config.AREA_SIZE, 2)
                self.pause_timers[i] = config.PAUSE_TIME
                self.speeds[i] = self.rng.uniform(
                    config.MIN_SPEED, config.MAX_SPEED)
            else:
                step = min(self.speeds[i] * dt, dist)
                self.positions[i] += (direction / dist) * step
                self.positions[i] = np.clip(
                    self.positions[i], 0, config.AREA_SIZE)

    def _drain_energy(self, dt: float) -> None:
        """Energy model: tx + rx + idle."""
        bits = config.PKT_SIZE * 8
        for i in range(self.n_nodes):
            if self.energies[i] <= 0:
                continue
            n_nb = len(self._get_neighbours(i))
            tx_cost = config.E_TX * bits * config.DATA_RATE * dt
            rx_cost = config.E_RX * bits * n_nb * dt * 0.1
            self.energies[i] = max(0.0, self.energies[i] - tx_cost - rx_cost)

    def _build_feature_matrix(self) -> np.ndarray:
        """Build 9-dim feature matrix for GNN."""
        n = self.n_nodes
        features = np.zeros((n, config.GNN_INPUT_DIM))
        rt_vals  = self.trust_mgr.get_all_rt()
        for i in range(n):
            features[i, 0] = self.positions[i, 0] / config.AREA_SIZE
            features[i, 1] = self.positions[i, 1] / config.AREA_SIZE
            features[i, 2] = self.energies[i] / config.E_INITIAL
            features[i, 3] = rt_vals[i]
            features[i, 4] = self.trust_mgr.get_stability(i)
            features[i, 5] = float(np.max(self.cluster_membership[i]))
            features[i, 6] = min(1.0, len(self._get_neighbours(i)) / 10.0)
            features[i, 7] = self.trust_mgr.get_suspicion(i)
            features[i, 8] = self.speeds[i] / config.MAX_SPEED
        return features

    def _build_adjacency(self) -> np.ndarray:
        adj = np.zeros((self.n_nodes, self.n_nodes))
        for i in range(self.n_nodes):
            for j in self._get_neighbours(i):
                adj[i, j] = 1.0
        return adj

    def _simulate_traffic(self, t: float) -> Tuple[int, int, float]:
        """Simulate CBR traffic, return (sent, received, total_delay)."""
        sent = recv = 0
        total_delay = 0.0
        rt_vals   = self.trust_mgr.get_all_rt()
        stability = np.array([self.trust_mgr.get_stability(i)
                               for i in range(self.n_nodes)])
        suspicion = np.array([self.trust_mgr.get_suspicion(i)
                               for i in range(self.n_nodes)])
        flagged   = self.trust_mgr.get_flagged()

        adj = self._build_adjacency()
        features = self._build_feature_matrix()
        scores = self.gnn.compute_routing_scores(
            features, adj, rt_vals, stability,
            self.energies, suspicion)

        # Simulate DATA_RATE packets per active source node
        active_nodes = [i for i in range(self.n_nodes)
                        if i not in flagged and self.energies[i] > 0]
        n_flows = min(len(active_nodes) // 2, 20)

        for flow_idx in range(max(n_flows, 1)):
            src = active_nodes[flow_idx % len(active_nodes)]
            dst = active_nodes[(flow_idx + max(n_flows,1)) % len(active_nodes)]
            if src == dst:
                continue
            for _ in range(config.DATA_RATE):
                sent += 1
                path_success, hops, delay = self._route_packet(
                    src, dst, scores, flagged, adj)
                if path_success:
                    recv += 1
                    total_delay += delay + hops * 0.001

        return sent, recv, total_delay

    def _route_packet(self, src: int, dst: int,
                      scores: np.ndarray, flagged: set,
                      adj: np.ndarray) -> Tuple[bool, int, float]:
        """Greedy routing via trust-aware scores."""
        current = src
        visited = {src}
        hops = 0
        max_hops = 10

        while current != dst and hops < max_hops:
            neighbours = [j for j in range(self.n_nodes)
                          if adj[current, j] > 0
                          and j not in visited
                          and j not in flagged
                          and self.energies[j] > 0]
            if not neighbours:
                return False, hops, 0.0

            # Pick best scored neighbour
            best_nb = max(neighbours, key=lambda j: scores[j])
            # Check if that node is malicious and will drop
            if best_nb in self.malicious_ids:
                at = self.attack_map[best_nb]
                if at == 'blackhole':
                    return False, hops, 0.0
                elif at == 'grayhole' and self.rng.random() < 0.6:
                    return False, hops, 0.0
                elif at == 'on_off' and (hops % 20) >= 10:
                    return False, hops, 0.0

            visited.add(best_nb)
            current = best_nb
            hops += 1

            # Check if destination reached via proximity
            dist = np.linalg.norm(self.positions[current] - self.positions[dst])
            if dist < config.TX_RANGE or current == dst:
                return True, hops, hops * 0.002

        return current == dst, hops, hops * 0.002

    def _evaluate_weights(self, weights: np.ndarray) -> float:
        """Fitness function for HLOA — quick evaluation."""
        self.gnn.set_weights(weights)
        rt_vals   = self.trust_mgr.get_all_rt()
        stability = np.array([self.trust_mgr.get_stability(i)
                               for i in range(self.n_nodes)])
        suspicion = np.array([self.trust_mgr.get_suspicion(i)
                               for i in range(self.n_nodes)])
        adj = self._build_adjacency()
        features = self._build_feature_matrix()
        scores = self.gnn.compute_routing_scores(
            features, adj, rt_vals, stability,
            self.energies, suspicion)

        flagged = self.trust_mgr.get_flagged()
        sent = recv = 0
        for _ in range(30):
            src = int(self.rng.integers(self.n_nodes))
            dst = int(self.rng.integers(self.n_nodes))
            if src == dst or src in flagged:
                continue
            sent += 1
            success, _, _ = self._route_packet(src, dst, scores, flagged, adj)
            if success:
                recv += 1

        pdr = recv / max(sent, 1)
        dr  = self.trust_mgr.get_detection_rate()
        ee  = float(rt_vals.mean())

        # False positive rate
        legitimate = set(range(self.n_nodes)) - set(self.malicious_ids)
        fp = len(legitimate & flagged) / max(len(legitimate), 1)
        fpr_penalty = fp  # penalise false positives

        fitness = (0.25 * pdr + 0.25 * dr + 0.20 * ee
                   + 0.20 * (self.trust_mgr.adaptive_threshold / config.THRESH_MAX)
                   + 0.10 * (1.0 - fpr_penalty))
        return float(np.clip(fitness, 0.0, 1.0))

    def _compute_metrics(self, t: float, sent: int, recv: int,
                         total_delay: float,
                         energy_start: float) -> Dict:
        rt_vals  = self.trust_mgr.get_all_rt()
        flagged  = self.trust_mgr.get_flagged()
        suspicion = np.array([self.trust_mgr.get_suspicion(i)
                               for i in range(self.n_nodes)])

        pdr = recv / max(sent, 1)
        tp  = (recv * config.PKT_SIZE * 8) / (t + 1e-9) / 1000.0
        ec  = (energy_start - self.energies.sum()) * 1000.0
        ee  = tp / (ec + 1e-9) * 1000.0
        delay = total_delay / max(recv, 1) * 1000.0
        dr  = self.trust_mgr.get_detection_rate() * 100.0

        # False positive rate
        legitimate = set(range(self.n_nodes)) - set(self.malicious_ids)
        fp_count = len(legitimate & flagged)
        fpr = fp_count / max(len(legitimate), 1) * 100.0

        # Per-attack detection
        per_attack_dr = {}
        for at in config.ATTACK_TYPES:
            at_nodes = {m for m in self.malicious_ids
                        if self.attack_map[m] == at}
            detected = len(at_nodes & flagged)
            per_attack_dr[at] = (detected / max(len(at_nodes), 1)) * 100.0

        return {
            'time': t,
            'pdr': pdr,
            'throughput_kbps': tp,
            'energy_mJ': ec,
            'energy_efficiency': ee,
            'delay_ms': delay,
            'detection_rate': dr,
            'false_positive_rate': fpr,
            'avg_trust': float(rt_vals.mean()),
            'adaptive_threshold': self.trust_mgr.adaptive_threshold,
            'per_attack_dr': per_attack_dr,
            'route_alpha': self.gnn.route_alpha,
            'route_beta': self.gnn.route_beta,
        }
