"""
simulation/behaviour.py
=======================
Ground-truth node behaviour (attack model).

Single source of truth for whether a node forwards a packet, used by BOTH
the watchdog observations fed to the trust manager and the data-packet
routing. Previously the trust manager and _route_packet each had their own
attack model, and in _route_packet on-off and collusion nodes never dropped
anything (`hops % 20 >= 10` is never true for hops < 10).

The trust manager never sees this object's attacker list.
"""

from typing import Dict, Iterable, Optional

import numpy as np

import config

ATTACKS = tuple(config.ATTACK_TYPES)


def assign_attack_types(malicious_ids, rng: np.random.Generator) -> Dict[int, str]:
    """
    Proportional assignment (largest remainder), as config.ATTACK_TYPES
    states. The original drew each type independently, so the realised mix
    varied from 1 to 6 blackholes out of 10 between runs.
    """
    n = len(malicious_ids)
    probs = np.array(list(config.ATTACK_TYPES.values()), dtype=float)
    probs = probs / probs.sum()
    raw = probs * n
    counts = np.floor(raw).astype(int)
    remainder = n - counts.sum()
    for k in np.argsort(-(raw - counts), kind='stable')[:remainder]:
        counts[k] += 1
    types = [t for t, c in zip(ATTACKS, counts) for _ in range(c)]
    ids = list(malicious_ids)
    rng.shuffle(ids)
    return {int(m): t for m, t in zip(ids, types)}


class AttackBehaviour:

    def __init__(self, n_nodes: int, malicious_ids: Iterable[int],
                 attack_map: Dict[int, str], seed, active: bool = True):
        """
        active=False: every node behaves honestly (no-attack reference),
        with the same random streams as the attacked run.
        """
        self.malicious = set(int(m) for m in malicious_ids)
        self.attack_map = dict(attack_map)
        unknown = set(self.attack_map.values()) - set(ATTACKS)
        if unknown:
            raise ValueError(f"unknown attack type(s): {unknown}")
        self.active = active
        ss = seed if isinstance(seed, np.random.SeedSequence) \
            else np.random.SeedSequence(seed)
        data_ss, watch_ss, phase_ss = ss.spawn(3)
        # Separate streams for data packets and watchdog observations, one
        # per node, so that variants that route differently still see the
        # same watchdog draws (common random numbers).
        self._rngs = {
            'data': [np.random.default_rng(s) for s in data_ss.spawn(n_nodes)],
            'watch': [np.random.default_rng(s) for s in watch_ss.spawn(n_nodes)],
        }
        self._phase = np.random.default_rng(phase_ss).uniform(
            0.0, config.ONOFF_PERIOD, n_nodes)
        self.colluders = {m for m in self.malicious
                          if self.attack_map.get(m) == 'collusion'}

    def attack_of(self, node_id: int) -> Optional[str]:
        if not self.active or node_id not in self.malicious:
            return None
        return self.attack_map.get(node_id, 'blackhole')

    def forwards(self, node_id: int, t: float, stream: str = 'data') -> bool:
        return self.decide(node_id, t, self._rngs[stream][node_id].random())

    def decide(self, node_id: int, t: float, u: float) -> bool:
        """Forwarding decision for a given uniform draw u."""
        attack = self.attack_of(node_id)
        if attack is None:
            return u >= config.BENIGN_FAILURE
        if attack == 'blackhole':
            return False
        if attack == 'grayhole':
            return u >= config.GRAYHOLE_DROP
        if attack == 'on_off':
            cycle = (t + self._phase[node_id]) % config.ONOFF_PERIOD
            if cycle < config.ONOFF_DUTY * config.ONOFF_PERIOD:
                return u >= config.BENIGN_FAILURE
            return False
        if attack == 'collusion':
            return u >= config.COLLUSION_DROP
        raise AssertionError(attack)

    def recommend(self, recommender: int, target: int, true_dt: float) -> float:
        """Colluders ballot-stuff each other and badmouth everyone else."""
        if not self.active or recommender not in self.colluders:
            return true_dt
        return 1.0 if target in self.colluders else 0.0
