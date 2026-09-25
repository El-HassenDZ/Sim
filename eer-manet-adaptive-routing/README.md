# AT-AEES-MANET — Adaptive Trust-Aware Energy-Efficient Secure Routing

> Extends the EER-MANET-EFIAGNN base paper with adaptive trust thresholds, on-off attack detection, and collusion filtering. Achieves **Detection Rate 88.50%** (+9.24pp over base paper) with **False Positive Rate 2.8%** across 10 simulation runs on 100-node MANETs.

---

## Key Results (vs Base Paper — EER-MANET-EFIAGNN)

| Metric | Base Paper | AT-AEES-MANET | Gain |
|---|---|---|---|
| Detection Rate (%) | 79.26 | **88.50** | **+9.24pp** |
| False Positive Rate (%) | — | **2.8** | — |
| Throughput (kbps) | 1263.9 | **1298.7** | **+2.76%** |
| Delay (ms) | 0.13 | **0.09** | **−30.8%** |
| On-Off Attack Detected | ✗ | **85.3%** | Novel |
| Collusion Detected | ✗ | **83.7%** | Novel |

All improvements statistically significant (paired t-test p < 0.001) except detection rate (p=0.0697).

---

## Ablation Study

| Variant | DR (%) | FPR (%) | TP (kbps) | Delay (ms) |
|---|---|---|---|---|
| EER-MANET-EFIAGNN (base) | 79.26 | — | 1263.9 | 0.13 |
| + Sliding-Window Trust | 82.10 | — | 1271.4 | 0.11 |
| + Adaptive Threshold | 80.95 | — | 1268.2 | 0.12 |
| + On-Off Attack Detection | 85.30 | — | 1279.6 | 0.10 |
| + Collusion Filter | 83.70 | — | 1274.1 | 0.11 |
| + False Positive Reduction | 79.80 | 4.1 | 1265.3 | 0.13 |
| **Full AT-AEES-MANET** | **88.50** | **2.8** | **1298.7** | **0.09** |

---

## Novel Contributions Over Base Paper

1. **Sliding-Window Trust** — Exponential decay over 5 windows of 10 interactions, replacing static trust accumulation
2. **Adaptive Trust Threshold** — Dynamically adjusts threshold [0.35–0.75] based on mobility, node density, trust variance, and packet loss
3. **On-Off Attack Detection** — Oscillation variance detector over 12-step window (threshold=0.12), isolates nodes for 20s then re-evaluates
4. **Collusion Filter** — Detects coordinated trust inflation among malicious nodes
5. **Extended GNN Feature Vector** — 9 features vs 6 in base paper (adds trust_stability, attack_suspicion, mobility_speed)

---

## Architecture

```
100-Node MANET Simulation (1000×1000m, 40s, 10 runs)
  10% malicious: blackhole(30%) grayhole(25%) on-off(25%) collusion(20%)
          │
          ▼
  AT-EFIAGNN (GNN Routing)          ← src/gnn/at_efiagnn.py
  9-feature input, 3 GNN layers
          │
          ├── FCMVC Clustering       ← src/clustering/fcmvc.py
          │   (10 clusters, FCM)
          │
          ├── Adaptive Trust Module  ← src/trust/adaptive_trust.py
          │   Sliding window · On-Off · Collusion filter
          │
          ├── HLOA Optimizer         ← src/optimization/hloa.py
          │   Pop=30, 50 iterations
          │
          └── MANET Environment      ← src/simulation/manet_env.py
              Random Waypoint mobility (1–10 m/s)
          │
          ▼
  Evaluation: DR · FPR · Throughput · Delay · Energy
              ← src/evaluation/metrics.py
```

---

## Project Structure

```
eer-manet-adaptive-routing/
├── main.py               # Entry point — run simulation
├── config.py             # All hyperparameters
├── src/
│   ├── gnn/              # AT-EFIAGNN graph neural network
│   ├── trust/            # Adaptive trust module
│   ├── simulation/       # MANET environment (mobility, energy, routing)
│   ├── optimization/     # HLOA optimizer
│   ├── clustering/       # FCMVC fuzzy clustering
│   ├── evaluation/       # Metrics and result aggregation
│   └── utils/            # Logger, timer, helpers
└── results/
    ├── simulation_results.json
    └── figures/          # 7 output plots
```

---

## How to Run

```bash
pip install -r requirements.txt

# Full simulation (100 nodes, 10 runs, ~20 min)
python main.py

# Quick sanity check (20 nodes, 2 runs, ~4 min)
python main.py --quick

# With ablation table
python main.py --ablation

# Skip plot generation
python main.py --no-plot
```

---

## Results Plots

| Plot | Description |
|---|---|
| `results/figures/detection_rate.png` | Detection rate over simulation time |
| `results/figures/false_positive_rate.png` | FPR over time |
| `results/figures/throughput_kbps.png` | Throughput comparison |
| `results/figures/delay_ms.png` | End-to-end delay |
| `results/figures/energy_efficiency.png` | Energy efficiency |
| `results/figures/energy_mJ.png` | Total energy consumption |
| `results/figures/adaptive_threshold.png` | Trust threshold adaptation over time |

---

## Tech Stack

`Python 3.11` · `numpy` · `scipy` · `matplotlib` · `networkx`

> No NS-3 dependency. Pure Python simulation — runs on any machine without C++ build setup.

---

## Simulation Config

Key parameters from `config.py`:

| Parameter | Value |
|---|---|
| Nodes | 100 |
| Area | 1000 × 1000 m |
| Simulation time | 40s |
| Runs | 10 |
| Malicious ratio | 10% |
| TX Range | 250m |
| Initial energy | 15.1 J/node |

---


