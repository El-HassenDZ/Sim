"""
trust/behaviour.py
==================
Ground-truth node behaviour (attack model), kept outside the trust manager
so that the defence never sees the attacker list.

Moved here from SlidingWindowTrustManager._simulate_behaviour, with:
- one RNG stream per node (SeedSequence.spawn), so a change in how often
  node A is solicited (e.g. because the defence isolates it) does not
  shift the random draws of node B;
- on-off period/duty and per-node random phase as parameters (previously
  a hard-coded 20 s cycle, identical phase for every attacker);
- an explicit false-recommendation model for colluders (previously the
  "collusion" attack was only a 25 % forwarding rate, so the collusion
  filter was never exercised).
"""

from typing import Dict, Iterable, Optional

import numpy as np

ATTACKS = ('blackhole', 'grayhole', 'on_off', 'collusion')


class AttackBehaviour:

    def __init__(self, n_nodes: int, malicious_ids: Iterable[int],
                 attack_map: Dict[int, str], seed: int,
                 benign_failure: float = 0.05,
                 grayhole_drop: float = 0.60,
                 collusion_drop: float = 0.75,
                 onoff_period: float = 20.0,
                 onoff_duty: float = 0.5,
                 onoff_random_phase: bool = True,
                 collusion_false_reports: bool = True):
        self.malicious = set(malicious_ids)
        self.attack_map = dict(attack_map)
        unknown = set(self.attack_map.values()) - set(ATTACKS)
        if unknown:
            raise ValueError(f"unknown attack type(s): {unknown}")
        self.benign_failure = benign_failure
        self.grayhole_drop = grayhole_drop
        self.collusion_drop = collusion_drop
        self.onoff_period = onoff_period
        self.onoff_duty = onoff_duty
        self.collusion_false_reports = collusion_false_reports

        ss = np.random.SeedSequence(seed)
        self._rngs = [np.random.default_rng(s) for s in ss.spawn(n_nodes)]
        phase_rng = np.random.default_rng(ss.spawn(1)[0])
        self._phase = (phase_rng.uniform(0.0, onoff_period, n_nodes)
                       if onoff_random_phase else np.zeros(n_nodes))
        self.colluders = {n for n in self.malicious
                          if self.attack_map.get(n) == 'collusion'}

    def attack_of(self, node_id: int) -> Optional[str]:
        if node_id not in self.malicious:
            return None
        return self.attack_map.get(node_id, 'blackhole')

    def forwards(self, node_id: int, t: float) -> bool:
        """Does node_id forward the packet it received at time t?"""
        u = self._rngs[node_id].random()
        attack = self.attack_of(node_id)
        if attack is None:
            return u >= self.benign_failure
        if attack == 'blackhole':
            return False
        if attack == 'grayhole':
            return u >= self.grayhole_drop
        if attack == 'on_off':
            cycle = (t + self._phase[node_id]) % self.onoff_period
            if cycle < self.onoff_duty * self.onoff_period:
                return u >= self.benign_failure     # behaving well
            return False                            # attacking
        if attack == 'collusion':
            return u >= self.collusion_drop
        raise AssertionError(attack)

    def recommend(self, recommender: int, target: int, true_dt: float) -> float:
        """
        Recommendation actually sent by `recommender` about `target`.
        Colluders ballot-stuff each other and badmouth everyone else.
        Pass this bound method as recommendation_fn to the trust manager.
        """
        if not self.collusion_false_reports or recommender not in self.colluders:
            return true_dt
        return 1.0 if target in self.colluders else 0.0
