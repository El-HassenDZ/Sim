"""
trust/adaptive_trust.py
=======================
Attack-Resilient Sliding-Window Trust Manager

Key innovations over base paper (EER-MANET-EFIAGNN):
1. Sliding-window trust  — old good behaviour fades, recent bad behaviour punished fast
2. Adaptive threshold    — context-aware threshold based on mobility/density/variance
3. On-off attack detector — detects nodes that alternate good/bad behaviour
4. Collusion filter      — majority-vote filtering on indirect trust reports
5. Trust stability score — extra feature fed into GNN (not in base paper)
"""

import numpy as np
from collections import deque
from dataclasses import dataclass, field
from typing import Dict, List, Tuple, Optional
import config


@dataclass
class InteractionRecord:
    """Single interaction record stored in sliding window."""
    timestamp: float
    forwarded: bool       # did node actually forward?
    delay: float          # observed forwarding delay (s)
    pkt_size: int         # packet size in bytes


@dataclass
class NodeTrustState:
    """Full trust state for a single node."""
    node_id: int
    windows: deque = field(default_factory=lambda: deque(maxlen=config.N_WINDOWS))
    current_window: List[InteractionRecord] = field(default_factory=list)
    trust_history: deque = field(default_factory=lambda: deque(maxlen=20))
    dt: float = 0.5           # direct trust
    idt: float = 0.5          # indirect trust
    rt: float = 0.5           # residual trust
    trust_stability: float = 1.0   # NEW: variance-based stability score
    oscillation_score: float = 0.0 # NEW: on-off attack indicator
    suspicion_level: float = 0.0   # NEW: combined attack suspicion
    isolated: bool = False
    isolation_until: float = 0.0
    re_eval_at: float = 0.0
    attack_type_suspected: Optional[str] = None


class SlidingWindowTrustManager:
    """
    Core trust engine for AT-AEES-MANET.

    Replaces the static DT/IDT/RT computation of the base paper with:
    - Exponentially-weighted sliding windows
    - Dynamic threshold computation
    - On-off attack detection via trust oscillation analysis
    - Collusion-resistant indirect trust aggregation
    """

    def __init__(self, n_nodes: int, rng: np.random.Generator,
                 malicious_ids: List[int], attack_map: Dict[int, str]):
        self.n_nodes    = n_nodes
        self.rng        = rng
        self.malicious  = set(malicious_ids)
        self.attack_map = attack_map           # node_id -> attack type

        # Per-node trust state
        self.states: Dict[int, NodeTrustState] = {
            i: NodeTrustState(node_id=i) for i in range(n_nodes)
        }

        # Global adaptive threshold
        self.adaptive_threshold = config.TRUST_THRESHOLD_BASE
        self.flagged: set = set()   # currently excluded nodes

    # ── Public API ────────────────────────────────────────────

    def record_interaction(self, observer: int, target: int,
                           timestamp: float, delay: float = 0.01) -> None:
        """Observer records an interaction with target node."""
        state = self.states[target]
        if state.isolated and timestamp < state.isolation_until:
            return  # skip interactions while isolated

        # Simulate behaviour based on attack type
        forwarded = self._simulate_behaviour(target, timestamp)

        rec = InteractionRecord(
            timestamp=timestamp,
            forwarded=forwarded,
            delay=delay,
            pkt_size=config.PKT_SIZE
        )
        state.current_window.append(rec)

        # Close window when full
        if len(state.current_window) >= config.WINDOW_SIZE:
            self._close_window(target, timestamp)

    def update_all(self, t: float, node_positions: np.ndarray,
                   node_energies: np.ndarray) -> None:
        """Called at each simulation timestep to update all trust values."""
        # Step 1: Flush partial windows
        for node_id in range(self.n_nodes):
            if self.states[node_id].current_window:
                self._close_window(node_id, t)

        # Step 2: Compute DT from sliding windows
        for node_id in range(self.n_nodes):
            self._compute_dt(node_id)

        # Step 3: Compute IDT with collusion filter
        for node_id in range(self.n_nodes):
            self._compute_idt(node_id, node_positions)

        # Step 4: Combine into RT
        for node_id in range(self.n_nodes):
            self._compute_rt(node_id, t)

        # Step 5: Detect on-off attackers
        for node_id in range(self.n_nodes):
            self._detect_on_off(node_id, t)

        # Step 6: Update adaptive threshold
        self._update_adaptive_threshold(node_positions, node_energies)

        # Step 7: Update flagged set
        self._update_flagged(t)

    def get_trust(self, node_id: int) -> float:
        return self.states[node_id].rt

    def get_stability(self, node_id: int) -> float:
        return self.states[node_id].trust_stability

    def get_suspicion(self, node_id: int) -> float:
        return self.states[node_id].suspicion_level

    def is_trusted(self, node_id: int) -> bool:
        state = self.states[node_id]
        if state.isolated:
            return False
        return state.rt >= self.adaptive_threshold

    def get_all_rt(self) -> np.ndarray:
        return np.array([self.states[i].rt for i in range(self.n_nodes)])

    def get_flagged(self) -> set:
        return self.flagged.copy()

    def get_detection_rate(self) -> float:
        detected = len(self.malicious & self.flagged)
        return detected / len(self.malicious) if self.malicious else 0.0

    # ── Core Trust Computation ────────────────────────────────

    def _close_window(self, node_id: int, timestamp: float) -> None:
        """Finalise current window and push to history."""
        state = self.states[node_id]
        if not state.current_window:
            return
        success = sum(1 for r in state.current_window if r.forwarded)
        total   = len(state.current_window)
        window_trust = success / total if total > 0 else 0.5
        state.windows.append(window_trust)
        state.current_window.clear()

    def _compute_dt(self, node_id: int) -> None:
        """
        Exponentially weighted sliding window direct trust.

        Older windows decay with factor DECAY_FACTOR^k.
        Recent bad behaviour has maximum impact.
        Formula:
            DT = Σ(decay^k * w_k) / Σ(decay^k)
            where k=0 is most recent window
        """
        state = self.states[node_id]
        windows = list(state.windows)
        if not windows:
            return

        total_weight = 0.0
        weighted_sum = 0.0
        for k, w in enumerate(reversed(windows)):  # k=0 = most recent
            weight = config.DECAY_FACTOR ** k
            weighted_sum += weight * w
            total_weight += weight

        state.dt = weighted_sum / total_weight if total_weight > 0 else 0.5

    def _compute_idt(self, node_id: int, positions: np.ndarray) -> None:
        """
        Collusion-resistant indirect trust.

        Instead of simple average (base paper), we:
        1. Only accept reports from trusted neighbours
        2. Apply majority filtering — discard outlier reports
        3. Weight by reporter's own trust score
        """
        state = self.states[node_id]
        pos_target = positions[node_id]

        reports = []
        reporter_trusts = []

        for nb_id in range(self.n_nodes):
            if nb_id == node_id:
                continue
            dist = np.linalg.norm(positions[nb_id] - pos_target)
            if dist > config.TX_RANGE:
                continue
            nb_state = self.states[nb_id]
            # Only trust reports from non-isolated, trusted neighbours
            if nb_state.isolated or nb_state.rt < config.TRUST_THRESHOLD_BASE * 0.8:
                continue
            reports.append(nb_state.dt)  # their direct observation of target
            reporter_trusts.append(nb_state.rt)

        if not reports:
            state.idt = state.dt  # fallback: use own observation
            return

        reports = np.array(reports)
        weights = np.array(reporter_trusts)

        # Collusion filter: remove reports deviating > 2 std from median
        median = np.median(reports)
        std    = np.std(reports) + 1e-9
        valid  = np.abs(reports - median) < 2.0 * std
        if valid.sum() == 0:
            valid = np.ones(len(reports), dtype=bool)

        filtered_reports = reports[valid]
        filtered_weights = weights[valid]
        filtered_weights /= filtered_weights.sum()

        state.idt = float(np.dot(filtered_reports, filtered_weights))

    def _compute_rt(self, node_id: int, t: float) -> None:
        """Combine DT and IDT into RT, update history and stability."""
        state = self.states[node_id]
        rt = config.TRUST_ALPHA * state.dt + config.TRUST_BETA * state.idt
        rt = float(np.clip(rt, 0.0, 1.0))
        state.rt = rt
        state.trust_history.append(rt)

        # Trust stability = inverse of variance over recent history
        if len(state.trust_history) >= 3:
            variance = float(np.var(list(state.trust_history)))
            state.trust_stability = float(np.exp(-10.0 * variance))
        else:
            state.trust_stability = 1.0

    def _detect_on_off(self, node_id: int, t: float) -> None:
        """
        On-Off attack detection via oscillation scoring.

        An on-off attacker shows HIGH variance in trust history —
        they behave well, then attack, then behave well again.
        We detect this by computing rolling variance over OSC_WINDOW steps.
        If variance exceeds OSC_THRESHOLD → flag as oscillating attacker.
        """
        state = self.states[node_id]
        history = list(state.trust_history)

        if len(history) < config.OSC_WINDOW:
            return

        recent = history[-config.OSC_WINDOW:]
        variance = float(np.var(recent))
        # Count direction changes (oscillation count)
        changes = sum(
            1 for i in range(1, len(recent) - 1)
            if (recent[i] - recent[i-1]) * (recent[i+1] - recent[i]) < 0
        )
        oscillation_score = variance * (1 + changes / config.OSC_WINDOW)
        state.oscillation_score = oscillation_score

        # Combined suspicion
        suspicion = state.suspicion_level * 0.4  # carry forward memory
        # Low RT
        if state.rt < self.adaptive_threshold:
            suspicion += 0.35
        # High oscillation
        if oscillation_score > config.OSC_THRESHOLD:
            suspicion += 0.40
            state.attack_type_suspected = 'on_off'
        # Low stability
        if state.trust_stability < 0.3:
            suspicion += 0.20
        # Previously flagged penalty
        if node_id in self.flagged:
            suspicion += 0.15

        state.suspicion_level = float(np.clip(suspicion, 0.0, 1.0))

        # Isolate if highly suspicious
        if suspicion >= 0.6 and not state.isolated:
            state.isolated = True
            state.isolation_until = t + config.ISOLATION_TIME
            state.re_eval_at = t + config.RE_EVAL_AFTER

    def _update_adaptive_threshold(self, positions: np.ndarray,
                                   energies: np.ndarray) -> None:
        """
        Dynamically compute trust threshold based on network context.

        threshold = base + Δ_mobility + Δ_density + Δ_variance + Δ_loss
        Clamped to [THRESH_MIN, THRESH_MAX]

        Logic:
        - High mobility  → stricter threshold (harder to verify nodes)
        - Low density    → stricter threshold (fewer witnesses)
        - High variance  → stricter threshold (network under stress)
        - High loss      → stricter threshold (possible ongoing attack)
        """
        # Mobility estimate: average pairwise distance change proxy
        # (use energy depletion rate as proxy for mobility-induced overhead)
        avg_energy = float(np.mean(energies))
        energy_fraction = avg_energy / config.E_INITIAL
        mobility_delta = config.THRESH_MOBILITY_W * (1.0 - energy_fraction) * 0.3

        # Density: average neighbours within TX_RANGE
        n_neighbours = []
        for i in range(len(positions)):
            dists = np.linalg.norm(positions - positions[i], axis=1)
            n_neighbours.append(np.sum(dists < config.TX_RANGE) - 1)
        avg_density = float(np.mean(n_neighbours)) / self.n_nodes
        density_delta = config.THRESH_DENSITY_W * (0.5 - avg_density)

        # Trust variance across network
        rt_values = np.array([self.states[i].rt for i in range(self.n_nodes)])
        trust_variance = float(np.var(rt_values))
        variance_delta = config.THRESH_VARIANCE_W * trust_variance

        # Packet loss proxy: mean of (1 - dt) for all nodes
        mean_loss = float(np.mean([1.0 - self.states[i].dt
                                   for i in range(self.n_nodes)]))
        loss_delta = config.THRESH_LOSS_W * mean_loss * 0.3

        threshold = (config.TRUST_THRESHOLD_BASE
                     + mobility_delta + density_delta
                     + variance_delta + loss_delta)
        self.adaptive_threshold = float(
            np.clip(threshold, config.THRESH_MIN, config.THRESH_MAX))

    def _update_flagged(self, t: float) -> None:
        """Update set of excluded nodes based on RT and isolation status."""
        self.flagged.clear()
        for node_id, state in self.states.items():
            # Re-evaluate isolated nodes
            if state.isolated and t >= state.re_eval_at:
                if state.rt > self.adaptive_threshold * 0.8:
                    state.isolated = False
                    # Suspicion decays slowly — never fully resets for caught nodes
                    state.suspicion_level = max(0.35, state.suspicion_level * 0.70)
                elif t >= state.isolation_until:
                    state.isolated = False
                    state.suspicion_level = max(0.30, state.suspicion_level * 0.80)

            # Flag based on combined criteria
            is_bad = (
                state.rt < self.adaptive_threshold
                or state.isolated
                or state.suspicion_level >= 0.8
            )
            if is_bad:
                self.flagged.add(node_id)

    def _simulate_behaviour(self, node_id: int, t: float) -> bool:
        """
        Simulate whether a node actually forwards a packet.
        Malicious nodes behave according to their attack type.
        """
        if node_id not in self.malicious:
            # Legitimate node: small chance of natural failure
            return self.rng.random() > 0.05

        attack = self.attack_map.get(node_id, 'blackhole')

        if attack == 'blackhole':
            return False  # always drops

        elif attack == 'grayhole':
            # Drops selectively — 60% of the time
            return self.rng.random() > 0.60

        elif attack == 'on_off':
            # Alternates: good for 10s then bad for 10s
            cycle = t % 20.0
            if cycle < 10.0:
                return self.rng.random() > 0.05   # behaving well
            else:
                return False                        # attacking

        elif attack == 'collusion':
            # Mostly bad but sometimes good to maintain plausible trust
            return self.rng.random() > 0.75

        return False
