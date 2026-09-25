import numpy as np
from typing import List
import config

class FCMVC:
    def __init__(self, n_clusters, rng):
        self.n_clusters = n_clusters
        self.rng = rng
        self.membership = None
        self.centroids = None
        self.labels = None

    def fit(self, positions, energies, trust_scores, trusted_mask):
        n = len(positions)
        E_norm = energies / config.E_INITIAL
        features = np.column_stack([positions / config.AREA_SIZE, E_norm, trust_scores])
        U = self.rng.random((n, self.n_clusters))
        U = U / U.sum(axis=1, keepdims=True)
        for _ in range(config.FCM_MAX_ITER):
            U_old = U.copy()
            m = config.FCM_FUZZINESS
            Um = U ** m
            centroids = (Um.T @ features) / (Um.sum(axis=0)[:, None] + 1e-10)
            dist = np.zeros((n, self.n_clusters))
            for k in range(self.n_clusters):
                diff = features - centroids[k]
                dist[:, k] = np.linalg.norm(diff, axis=1) + 1e-10
            for k in range(self.n_clusters):
                ratio = dist[:, k:k+1] / (dist + 1e-10)
                U[:, k] = 1.0 / (ratio ** (2.0 / (m - 1))).sum(axis=1)
            U = U / (U.sum(axis=1, keepdims=True) + 1e-10)
            if np.max(np.abs(U - U_old)) < config.FCM_EPSILON:
                break
        self.membership = U
        self.centroids = centroids
        self.labels = np.argmax(U, axis=1)

    def select_cluster_heads(self, positions, energies, trust_scores,
                              trust_stability, suspicion, flagged):
        ch_list = []
        for k in range(self.n_clusters):
            members = [i for i in range(len(positions))
                       if self.labels[i] == k and i not in flagged]
            if not members:
                members = [i for i in range(len(positions)) if i not in flagged]
            if not members:
                ch_list.append(0)
                continue
            best_score = -1.0
            best_ch = members[0]
            cluster_positions = positions[members]
            for idx in members:
                dists = np.linalg.norm(cluster_positions - positions[idx], axis=1)
                centrality = 1.0 / (np.mean(dists) + 1e-3)
                centrality = min(centrality / 0.05, 1.0)
                score = (0.35 * trust_scores[idx]
                       + 0.25 * (energies[idx] / config.E_INITIAL)
                       + 0.20 * centrality
                       + 0.15 * trust_stability[idx]
                       - 0.05 * suspicion[idx])
                if score > best_score:
                    best_score = score
                    best_ch = idx
            ch_list.append(best_ch)
        return ch_list
