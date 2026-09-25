"""
trust/adaptive_trust.py
=======================
Attack-Resilient Sliding-Window Trust Manager (corrected version)

Same five mechanisms as the original file (sliding-window DT, adaptive
threshold, on-off detector, collusion filter, stability score), with the
following corrections (see the review for the evidence behind each one):

- The manager no longer receives the attacker list or generates behaviour.
  It only consumes observations (observer, target, forwarded?) supplied by
  the simulator; ground truth lives in simulation/behaviour.py and the
  metrics that need it live in evaluation/metrics.py.
- Direct trust is kept per (observer, target) pair; the `observer`
  argument was previously ignored.
- Indirect trust aggregates what neighbours report ABOUT THE TARGET
  (DT_k(target)), not the neighbours' own trust value (DT(k)).
- Partial windows are no longer flushed at every timestep, and windows
  are weighted by their number of observations (Beta prior), so
  WINDOW_SIZE keeps its meaning and single-sample windows no longer
  produce 0/1 trust values.
- Evidence keeps being recorded while a node is isolated: whether traffic
  is still routed through it is the caller's (routing) decision.
- The adaptive threshold is computed before suspicion is scored, and the
  mobility term uses measured node speed instead of residual energy.
- Suspicion (low RT) is scored from the first timestep; only the
  oscillation term needs OSC_WINDOW samples.
- is_trusted() and the flagged set use one definition.

Ablation switches (constructor flags) allow each component to be disabled
in the same simulator: sliding_window, adaptive_threshold, onoff_detector,
collusion_filter.

Hypothesis kept from the original design (state it in any write-up):
the network-wide RT(j) used for flagging/isolation is the median of the
per-observer RT_i(j) over the observers holding evidence on j, i.e. a
centralised IDS view (or an ideal consensus), not a per-node decision.
"""

from collections import deque
from dataclasses import dataclass, field
from typing import Callable, Dict, Optional, Tuple

import numpy as np

import config

MOBILITY_REF_SPEED = getattr(config, 'MOBILITY_REF_SPEED', config.MAX_SPEED)
COLLUSION_FILTER = getattr(config, 'COLLUSION_FILTER', 'mad')      # 'mad' | 'std'

# report = recommendation_fn(recommender, target, true_dt)
RecommendationFn = Callable[[int, int, float], float]


@dataclass
class PairEvidence:
    """Forwarding evidence gathered by one observer about one target."""
    windows: deque = field(
        default_factory=lambda: deque(maxlen=config.N_WINDOWS))  # (succ, n)
    decay: float = config.DECAY_FACTOR
    cur_succ: int = 0
    cur_n: int = 0
    total_n: int = 0

    def add(self, forwarded: bool) -> None:
        self.cur_succ += int(forwarded)
        self.cur_n += 1
        self.total_n += 1
        if self.cur_n >= config.WINDOW_SIZE:
            self.windows.append((self.cur_succ, self.cur_n))
            self.cur_succ = 0
            self.cur_n = 0

    def direct_trust(self) -> float:
        """
        DT = (Σ d^k s_k + 1) / (Σ d^k n_k + 2), k = 0 most recent window
        (the open window, if non-empty, is k = 0). Beta(1,1) prior: no
        evidence gives 0.5, one success gives 2/3 rather than 1.
        """
        items = list(self.windows)
        if self.cur_n > 0:
            items.append((self.cur_succ, self.cur_n))
        s_w = n_w = 0.0
        for k, (s, n) in enumerate(reversed(items)):
            weight = self.decay ** k
            s_w += weight * s
            n_w += weight * n
        return (s_w + 1.0) / (n_w + 2.0)


@dataclass
class NodeTrustState:
    """Network-wide (aggregated) trust state for a single node."""
    node_id: int
    trust_history: deque = field(default_factory=lambda: deque(maxlen=20))
    dt: float = 0.5           # aggregated direct trust
    idt: float = 0.5          # aggregated indirect trust
    rt: float = 0.5           # aggregated residual trust
    trust_stability: float = 1.0
    oscillation_score: float = 0.0
    suspicion_level: float = 0.0
    isolated: bool = False
    isolation_until: float = 0.0
    re_eval_at: float = 0.0
    evidence_at_isolation: int = 0
    attack_type_suspected: Optional[str] = None


class SlidingWindowTrustManager:
    """
    Core trust engine for AT-AEES-MANET.

    Usage per timestep:
        for each observed forwarding attempt:
            record_observation(observer, target, t, forwarded)
        update_all(t, positions)
    """

    def __init__(self, n_nodes: int,
                 recommendation_fn: Optional[RecommendationFn] = None,
                 sliding_window: bool = True,
                 adaptive_threshold: bool = True,
                 onoff_detector: bool = True,
                 collusion_filter: bool = True):
        self.n_nodes = n_nodes
        self.sliding_window = sliding_window
        self.use_adaptive_threshold = adaptive_threshold
        self.onoff_detector = onoff_detector
        self.collusion_filter = collusion_filter
        self._obs_count = np.zeros(n_nodes, dtype=np.int64)
        # Identity by default: recommenders report their true DT. The
        # attack model may substitute false reports (badmouthing /
        # ballot-stuffing) without the manager knowing who lies.
        self.recommendation_fn = recommendation_fn

        self.evidence: Dict[Tuple[int, int], PairEvidence] = {}
        self.states: Dict[int, NodeTrustState] = {
            i: NodeTrustState(node_id=i) for i in range(n_nodes)}

        self.adaptive_threshold = config.TRUST_THRESHOLD_BASE
        self.flagged: set = set()
        self._prev_positions: Optional[np.ndarray] = None
        self._prev_t: Optional[float] = None

        # Per-observer matrices, refreshed by update_all
        self.DT = np.full((n_nodes, n_nodes), 0.5)
        self.HAS_EV = np.zeros((n_nodes, n_nodes), dtype=bool)
        self.RT = np.full((n_nodes, n_nodes), 0.5)

    # ── Public API ────────────────────────────────────────────

    def record_observation(self, observer: int, target: int, timestamp: float,
                           forwarded: bool, delay: Optional[float] = None) -> None:
        """
        Observer reports whether target forwarded a packet it handed over.
        `delay` is accepted for API compatibility; it is not part of the
        trust formula (the original stored it without using it either).
        """
        if observer == target:
            return
        key = (observer, target)
        ev = self.evidence.get(key)
        if ev is None:
            if self.sliding_window:
                ev = PairEvidence()
            else:   # static accumulation: no decay, unbounded history
                ev = PairEvidence(windows=deque(), decay=1.0)
            self.evidence[key] = ev
        ev.add(bool(forwarded))
        self._obs_count[target] += 1

    def update_all(self, t: float, node_positions: np.ndarray,
                   node_energies: Optional[np.ndarray] = None) -> None:
        """
        Called at each simulation timestep. `node_energies` is accepted for
        API compatibility and no longer used (it served as a mobility proxy).
        """
        positions = np.asarray(node_positions, dtype=float)
        adjacency = self._adjacency(positions)

        self._compute_dt_matrix()
        self._compute_rt_matrix(adjacency)
        self._aggregate(t)
        self._update_adaptive_threshold(t, positions, adjacency)
        for node_id in range(self.n_nodes):
            self._detect_on_off(node_id, t)
        self._update_flagged(t)

    def get_trust(self, node_id: int) -> float:
        return self.states[node_id].rt

    def get_trust_of(self, observer: int, target: int) -> float:
        """Local view: RT held by `observer` about `target`."""
        return float(self.RT[observer, target])

    def get_stability(self, node_id: int) -> float:
        return self.states[node_id].trust_stability

    def get_suspicion(self, node_id: int) -> float:
        return self.states[node_id].suspicion_level

    def is_trusted(self, node_id: int) -> bool:
        return node_id not in self.flagged

    def get_all_rt(self) -> np.ndarray:
        return np.array([self.states[i].rt for i in range(self.n_nodes)])

    def get_flagged(self) -> set:
        return self.flagged.copy()

    # ── Core Trust Computation ────────────────────────────────

    def _adjacency(self, positions: np.ndarray) -> np.ndarray:
        diff = positions[:, None, :] - positions[None, :, :]
        dist = np.linalg.norm(diff, axis=2)
        adj = dist <= config.TX_RANGE
        np.fill_diagonal(adj, False)
        return adj

    def _compute_dt_matrix(self) -> None:
        self.DT.fill(0.5)
        self.HAS_EV.fill(False)
        for (i, j), ev in self.evidence.items():
            self.DT[i, j] = ev.direct_trust()
            self.HAS_EV[i, j] = ev.total_n > 0

    def _report(self, k: int, j: int, true_dt: float) -> float:
        if self.recommendation_fn is None:
            return true_dt
        return float(np.clip(self.recommendation_fn(k, j, true_dt), 0.0, 1.0))

    def _filter_outliers(self, reports: np.ndarray) -> np.ndarray:
        median = np.median(reports)
        if COLLUSION_FILTER == 'std':            # original rule
            spread = np.std(reports) + 1e-9
            valid = np.abs(reports - median) < 2.0 * spread
        else:
            # MAD is not inflated by the outliers it must remove.
            mad = 1.4826 * np.median(np.abs(reports - median))
            valid = np.abs(reports - median) <= 2.0 * max(mad, 0.05)
        if not valid.any():
            valid = np.ones(len(reports), dtype=bool)
        return valid

    def _indirect_trust(self, i: int, j: int, adjacency: np.ndarray) -> float:
        """
        IDT_i(j): what i's one-hop neighbours k report about j, weighted by
        i's own direct trust in k. Only recommenders i trusts
        (DT_i(k) >= 0.8 * TRUST_THRESHOLD_BASE, as in the original) and
        that hold evidence on j are consulted.
        """
        cand = adjacency[i] & self.HAS_EV[:, j]
        cand[j] = False
        cand &= self.HAS_EV[i]       # i must know the recommender
        ks = np.nonzero(cand)[0]
        if ks.size:
            w = self.DT[i, ks]
            keep = w >= config.TRUST_THRESHOLD_BASE * 0.8
            ks, w = ks[keep], w[keep]
        if ks.size == 0:
            return float(self.DT[i, j])  # fallback: own observation
        reports = np.array([self._report(int(k), j, self.DT[k, j]) for k in ks])
        if self.collusion_filter:
            valid = self._filter_outliers(reports)
        else:
            valid = np.ones(len(reports), dtype=bool)
        w = w[valid]
        return float(np.dot(reports[valid], w / w.sum()))

    def _compute_rt_matrix(self, adjacency: np.ndarray) -> None:
        self.IDT = self.DT.copy()
        for i in range(self.n_nodes):
            for j in np.nonzero(self.HAS_EV[i])[0]:
                self.IDT[i, j] = self._indirect_trust(i, int(j), adjacency)
        rt = config.TRUST_ALPHA * self.DT + config.TRUST_BETA * self.IDT
        self.RT = np.clip(rt, 0.0, 1.0)

    def _aggregate(self, t: float) -> None:
        """Network-wide view: median over observers holding evidence."""
        for j in range(self.n_nodes):
            state = self.states[j]
            obs = self.HAS_EV[:, j]
            if obs.any():
                state.dt = float(np.median(self.DT[obs, j]))
                state.idt = float(np.median(self.IDT[obs, j]))
                state.rt = float(np.median(self.RT[obs, j]))
            state.trust_history.append(state.rt)
            if len(state.trust_history) >= 3:
                variance = float(np.var(list(state.trust_history)))
                state.trust_stability = float(np.exp(-10.0 * variance))
            else:
                state.trust_stability = 1.0

    def _detect_on_off(self, node_id: int, t: float) -> None:
        """Oscillation + low-RT suspicion scoring (coefficients unchanged)."""
        state = self.states[node_id]
        history = list(state.trust_history)

        oscillating = False
        if self.onoff_detector and len(history) >= config.OSC_WINDOW:
            recent = history[-config.OSC_WINDOW:]
            variance = float(np.var(recent))
            changes = sum(
                1 for i in range(1, len(recent) - 1)
                if (recent[i] - recent[i - 1]) * (recent[i + 1] - recent[i]) < 0)
            state.oscillation_score = variance * (1 + changes / config.OSC_WINDOW)
            oscillating = state.oscillation_score > config.OSC_THRESHOLD
        state.attack_type_suspected = 'on_off' if oscillating else None

        suspicion = state.suspicion_level * 0.4
        if state.rt < self.adaptive_threshold:
            suspicion += 0.35
        if oscillating:
            suspicion += 0.40
        if state.trust_stability < 0.3:
            suspicion += 0.20
        if node_id in self.flagged:
            suspicion += 0.15
        state.suspicion_level = float(np.clip(suspicion, 0.0, 1.0))

        if state.suspicion_level >= 0.6 and not state.isolated:
            state.isolated = True
            state.isolation_until = t + config.ISOLATION_TIME
            state.re_eval_at = t + config.RE_EVAL_AFTER
            state.evidence_at_isolation = self._evidence_count(node_id)

    def _evidence_count(self, node_id: int) -> int:
        return int(self._obs_count[node_id])

    def _update_adaptive_threshold(self, t: float, positions: np.ndarray,
                                   adjacency: np.ndarray) -> None:
        """
        threshold = base + Δ_mobility + Δ_density + Δ_variance + Δ_loss,
        clamped to [THRESH_MIN, THRESH_MAX].
        """
        mobility_delta = 0.0
        if self._prev_positions is not None and t > self._prev_t:
            speed = np.linalg.norm(positions - self._prev_positions, axis=1)
            mean_speed = float(np.mean(speed)) / (t - self._prev_t)
            mobility_delta = config.THRESH_MOBILITY_W * min(
                1.0, mean_speed / MOBILITY_REF_SPEED)
        self._prev_positions = positions.copy()
        self._prev_t = t

        avg_density = float(adjacency.sum(axis=1).mean()) / self.n_nodes
        density_delta = config.THRESH_DENSITY_W * (0.5 - avg_density)

        rt_values = self.get_all_rt()
        variance_delta = config.THRESH_VARIANCE_W * float(np.var(rt_values))

        mean_loss = float(np.mean([1.0 - self.states[i].dt
                                   for i in range(self.n_nodes)]))
        loss_delta = config.THRESH_LOSS_W * mean_loss * 0.3

        if not self.use_adaptive_threshold:
            self.adaptive_threshold = config.TRUST_THRESHOLD_BASE
            return
        threshold = (config.TRUST_THRESHOLD_BASE + mobility_delta
                     + density_delta + variance_delta + loss_delta)
        self.adaptive_threshold = float(
            np.clip(threshold, config.THRESH_MIN, config.THRESH_MAX))

    def _update_flagged(self, t: float) -> None:
        self.flagged.clear()
        for node_id, state in self.states.items():
            if state.isolated and t >= state.re_eval_at:
                # Early release only on evidence gathered after isolation;
                # otherwise the RT being judged is the one that caused it.
                fresh = self._evidence_count(node_id) > state.evidence_at_isolation
                if fresh and state.rt > self.adaptive_threshold * 0.8:
                    state.isolated = False
                    state.suspicion_level = max(0.35, state.suspicion_level * 0.70)
                elif t >= state.isolation_until:
                    state.isolated = False
                    state.suspicion_level = max(0.30, state.suspicion_level * 0.80)

            if (state.rt < self.adaptive_threshold or state.isolated
                    or state.suspicion_level >= 0.8):
                self.flagged.add(node_id)
