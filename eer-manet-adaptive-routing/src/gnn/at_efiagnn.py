"""
gnn/at_efiagnn.py
=================
Adaptive Trust - Explicit Feature Interaction Aware GNN

Enhancements over base EFIAGNN (base paper):
1. Input dimension: 9 features vs 6 (adds trust_stability, attack_suspicion, speed)
2. Trust-gated aggregation: aggregation weight = RT × stability (not just RT)
3. Attack suspicion masking: suspicious nodes contribute near-zero to aggregation
4. Adaptive routing metric weights: α, β, γ shift based on network stress
"""

import numpy as np
from typing import List, Tuple
import config


class ATEFIAGNNLayer:
    """Single layer of the AT-EFIAGNN."""

    def __init__(self, in_dim: int, out_dim: int, rng: np.random.Generator):
        # Standard message passing weights
        self.W_self  = rng.standard_normal((in_dim, out_dim)) * 0.1
        self.W_neigh = rng.standard_normal((in_dim, out_dim)) * 0.1
        # Explicit feature interaction weights
        self.W_inter = rng.standard_normal((in_dim, out_dim)) * 0.1
        self.bias    = np.zeros(out_dim)

    def forward(self, H: np.ndarray, adj: np.ndarray,
                trust_weights: np.ndarray) -> np.ndarray:
        """
        H: (n, in_dim) node features
        adj: (n, n) adjacency (1 if connected, 0 otherwise)
        trust_weights: (n, n) edge weights = RT_j × stability_j

        Output: (n, out_dim)
        """
        n = H.shape[0]
        out = np.zeros((n, self.W_self.shape[1]))

        for i in range(n):
            # Self transform
            h_self = H[i] @ self.W_self

            # Trust-gated neighbour aggregation
            neighbours = np.where(adj[i] > 0)[0]
            if len(neighbours) > 0:
                weights = trust_weights[i, neighbours]
                w_sum = weights.sum() + 1e-10
                # Weighted message
                h_agg = np.zeros(H.shape[1])
                for j, w in zip(neighbours, weights):
                    h_agg += (w / w_sum) * H[j]
                h_neigh = h_agg @ self.W_neigh

                # Explicit feature interaction: i ⊗ neighbours
                h_interact = np.zeros(H.shape[1])
                for j, w in zip(neighbours, weights):
                    h_interact += (w / w_sum) * (H[i] * H[j])  # element-wise
                h_inter = h_interact @ self.W_inter
            else:
                h_neigh = np.zeros(self.W_neigh.shape[1])
                h_inter = np.zeros(self.W_inter.shape[1])

            out[i] = self._relu(h_self + h_neigh + h_inter + self.bias)

        return out

    def _relu(self, x: np.ndarray) -> np.ndarray:
        return np.maximum(0.0, x)

    def get_weights(self) -> np.ndarray:
        return np.concatenate([
            self.W_self.flatten(),
            self.W_neigh.flatten(),
            self.W_inter.flatten(),
            self.bias.flatten()
        ])

    def set_weights(self, w: np.ndarray) -> int:
        idx = 0
        shapes = [
            (self.W_self,  self.W_self.shape),
            (self.W_neigh, self.W_neigh.shape),
            (self.W_inter, self.W_inter.shape),
            (self.bias,    self.bias.shape),
        ]
        for arr, shape in shapes:
            size = int(np.prod(shape))
            arr[:] = w[idx:idx+size].reshape(shape)
            idx += size
        return idx


class ATEFIAGNN:
    """
    Full 3-layer AT-EFIAGNN network.

    Input features per node (9-dim):
        0: x position (normalised)
        1: y position (normalised)
        2: residual energy (normalised)
        3: residual trust RT
        4: trust stability
        5: cluster membership strength
        6: link quality
        7: attack suspicion score
        8: mobility speed (normalised)
    """

    def __init__(self, rng: np.random.Generator):
        in_dim = config.GNN_INPUT_DIM   # 9
        hid    = config.GNN_HIDDEN      # 128
        self.layers = [
            ATEFIAGNNLayer(in_dim, hid, rng),
            ATEFIAGNNLayer(hid,    hid, rng),
            ATEFIAGNNLayer(hid,    1,   rng),  # output: routing score per node
        ]
        self.weight_dim = self._count_weights()
        # Adaptive routing weights (updated dynamically)
        self.route_alpha = config.ROUTE_ALPHA
        self.route_beta  = config.ROUTE_BETA
        self.route_gamma = config.ROUTE_GAMMA

    def _count_weights(self) -> int:
        total = 0
        in_dim = config.GNN_INPUT_DIM
        hid    = config.GNN_HIDDEN
        # Layer 1: in→hid (W_self, W_neigh, W_inter, bias)
        total += 3 * in_dim * hid + hid
        # Layer 2: hid→hid
        total += 3 * hid * hid + hid
        # Layer 3: hid→1
        total += 3 * hid * 1 + 1
        return total

    def set_weights(self, w: np.ndarray) -> None:
        idx = 0
        for layer in self.layers:
            consumed = layer.set_weights(w[idx:])
            idx += consumed

    def get_weights(self) -> np.ndarray:
        return np.concatenate([l.get_weights() for l in self.layers])

    def forward(self, features: np.ndarray, adj: np.ndarray,
                trust_weights: np.ndarray) -> np.ndarray:
        """
        features: (n, 9)
        adj: (n, n)
        trust_weights: (n, n) — RT × stability gating

        Returns routing scores: (n,) — higher = better next-hop
        """
        H = features.copy()
        for layer in self.layers:
            H = layer.forward(H, adj, trust_weights)
        return H.flatten()

    def compute_routing_scores(self, features: np.ndarray, adj: np.ndarray,
                                trust_scores: np.ndarray,
                                trust_stability: np.ndarray,
                                energies: np.ndarray,
                                suspicion: np.ndarray) -> np.ndarray:
        """
        Full routing score = GNN_score × route_alpha × trust
                           + route_beta × energy_fraction
                           - route_gamma × suspicion_penalty
        """
        # Build trust-gated edge weights
        n = len(trust_scores)
        trust_w = np.zeros((n, n))
        for i in range(n):
            for j in range(n):
                if adj[i, j] > 0:
                    # Gate = RT_j × stability_j × (1 - suspicion_j)
                    trust_w[i, j] = (trust_scores[j]
                                     * trust_stability[j]
                                     * (1.0 - suspicion[j]))

        gnn_scores = self.forward(features, adj, trust_w)
        gnn_scores = (gnn_scores - gnn_scores.min()) / (gnn_scores.max() - gnn_scores.min() + 1e-10)

        # Composite routing metric
        energy_frac = energies / config.E_INITIAL
        composite = (self.route_alpha * trust_scores
                   + self.route_beta  * energy_frac
                   + 0.3 * gnn_scores
                   - self.route_gamma * suspicion)

        # Zero out flagged/suspicious nodes
        composite[suspicion > 0.8] = 0.0
        return composite

    def update_route_weights(self, avg_trust: float, avg_energy_frac: float,
                              attack_rate: float) -> None:
        """
        Dynamically adjust α, β, γ routing weights based on network state.

        Under attack (high attack_rate) → boost trust weight
        Low energy (low avg_energy_frac) → boost energy weight
        Normal conditions → balanced weights
        """
        base_a = config.ROUTE_ALPHA
        base_b = config.ROUTE_BETA
        base_g = config.ROUTE_GAMMA

        # Shift weights based on conditions
        attack_boost   = 0.15 * attack_rate
        energy_boost   = 0.10 * (1.0 - avg_energy_frac)

        self.route_alpha = min(0.70, base_a + attack_boost)
        self.route_beta  = min(0.50, base_b + energy_boost)
        self.route_gamma = min(0.40, base_g + 0.10 * attack_rate)

        # Renormalise so they sum to ~1
        total = self.route_alpha + self.route_beta + self.route_gamma
        self.route_alpha /= total
        self.route_beta  /= total
        self.route_gamma /= total
