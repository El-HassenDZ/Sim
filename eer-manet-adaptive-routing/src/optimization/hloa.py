import numpy as np
from typing import Callable, Tuple
import config

class HLOA:
    def __init__(self, dim, fitness_fn, rng, target_fitness=None):
        self.dim = dim
        # Stop early once this fitness is reached (e.g. PDR = 1.0): further
        # iterations cannot improve a saturated fitness.
        self.target_fitness = target_fitness
        self.fitness_fn = fitness_fn
        self.rng = rng
        self.pop_size = config.HLOA_POP
        self.max_iter = config.HLOA_MAX_ITER
        self.population = None
        self.fitness = None
        self.best_sol = None
        self.best_fit = -np.inf
        self.worst_sol = None
        self.fitness_history = []

    def _initialise(self):
        self.population = self.rng.uniform(-1, 1, (self.pop_size, self.dim))
        self.fitness = np.zeros(self.pop_size)

    def _evaluate_all(self):
        for i in range(self.pop_size):
            self.fitness[i] = self.fitness_fn(self.population[i])
        self._update_best_worst()

    def _update_best_worst(self):
        best_idx = int(np.argmax(self.fitness))
        worst_idx = int(np.argmin(self.fitness))
        self.best_sol = self.population[best_idx].copy()
        self.best_fit = self.fitness[best_idx]
        self.worst_sol = self.population[worst_idx].copy()

    def _clip(self, x):
        return np.clip(x, -1.0, 1.0)

    def _crypsis(self, i, t):
        rand_pos = self.rng.uniform(-1, 1, self.dim)
        step = (1 - t / self.max_iter) * config.HLOA_WEIGHT_DECAY
        return self._clip(self.population[i] + step * (rand_pos - self.population[i]))

    def _skin_change(self, i, t):
        scale = 1.0 - (t / self.max_iter) ** 2
        noise = self.rng.standard_normal(self.dim) * scale * 0.1
        return self._clip(self.population[i] + noise)

    def _blood_squirt(self, i, t):
        r = self.rng.random()
        direction = self.best_sol - self.population[i]
        jump = self.rng.uniform(-1, 1, self.dim) * r
        return self._clip(self.population[i] + 0.3 * direction + 0.2 * jump)

    def _escape_move(self, i):
        direction = self.population[i] - self.worst_sol
        norm = np.linalg.norm(direction) + 1e-10
        return self._clip(self.population[i] + 0.2 * direction / norm)

    def _hormone_update(self, i):
        r = self.rng.random(self.dim)
        return self._clip(self.population[i] + r * (self.best_sol - self.population[i]))

    def _select_best_candidate(self, candidates):
        scores = [self.fitness_fn(c) for c in candidates]
        k = int(np.argmax(scores))
        return candidates[k], scores[k]

    def optimise(self, verbose=True):
        self._initialise()
        self._evaluate_all()
        self.iterations_run = 0
        for iteration in range(1, self.max_iter + 1):
            if len(self.fitness_history) >= 3:
                recent = self.fitness_history[-3:]
                if max(recent) - min(recent) < 1e-6:
                    for wi in np.argsort(self.fitness)[:max(1, self.pop_size // 5)]:
                        self.population[wi] = self._clip(
                            self.best_sol + self.rng.normal(0, 0.2, self.dim))
                        self.fitness[wi] = self.fitness_fn(self.population[wi])
            for i in range(self.pop_size):
                candidates = [
                    self._crypsis(i, iteration),
                    self._skin_change(i, iteration),
                    self._blood_squirt(i, iteration),
                    self._escape_move(i),
                    self._hormone_update(i),
                ]
                # Reuse the score already computed (was evaluated twice)
                best_candidate, fit = self._select_best_candidate(candidates)
                if fit > self.fitness[i]:
                    self.population[i] = best_candidate
                    self.fitness[i] = fit
            self._update_best_worst()
            self.fitness_history.append(self.best_fit)
            self.iterations_run = iteration
            if self.target_fitness is not None and self.best_fit >= self.target_fitness:
                break
            if verbose and iteration % 10 == 0:
                print(f"  HLOA iter {iteration:3d}/{self.max_iter} | Best fitness: {self.best_fit:.6f}")
        return self.best_sol.copy(), self.best_fit
