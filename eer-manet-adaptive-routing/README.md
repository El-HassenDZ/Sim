# AT-AEES-MANET — Adaptive Trust-Aware Energy-Efficient Secure Routing (reviewed version)

Pure-Python, discrete-time (1 s) abstraction of a MANET with a trust layer, a
GNN-scored greedy geographic forwarder and four node-misbehaviour models.
**This is not ns-3 and not a packet-level simulator**: there is no MAC, no
queue, no route discovery (no AODV/OLSR), no control traffic. Delay is a
hop-count proxy and throughput is bounded by the offered load.

The received version of this repository (first commit touching this folder)
reported results that its own code does not produce; see `REVIEW.md`.

## What changed with respect to the received version

| Area | Received version | Now |
|---|---|---|
| Headline / ablation numbers | hard-coded in `main.py` (`print_ablation_table`) | computed from runs of each variant |
| Statistical test | "paired" t-test vs baseline samples synthesised as N(value, 5 %) | paired t-test between variants over the same seeds |
| Literature baselines | % improvement across simulators and units | removed |
| Attack on data packets | only blackhole and grayhole ever dropped (`hops % 20 >= 10` never true) | one behaviour model for data and watchdog |
| Trust manager | received the attacker list, generated behaviour, IDT = neighbours' own trust | observation-only, per-observer DT, IDT from reports about the target |
| Routing | next hop = best score regardless of destination | greedy geographic (must progress), trust/GNN score among candidates |
| Energy | every node charged 4 pkt/s regardless of traffic, `E_AMP` unused | first-order radio model per hop |
| Router training | HLOA on the test scenario, noisy fitness with ground-truth terms | HLOA on an independent training scenario, deterministic PDR fitness |
| Baselines in the same simulator | none | `no_defense`, `no_attack`, 4 leave-one-out ablations |
| `python main.py` | `ModuleNotFoundError: simulation` | runs |

## How to run

```bash
pip install -r requirements.txt
python main.py                  # 10 runs, 100 nodes, 7 variants (~5 min on 4 cores)
python main.py --quick          # 2 runs, 20 nodes, 10 s
python main.py --variants full,no_defense --runs 30
python -m pytest -q tests
```

Outputs go to `outputs/` (`simulation_results.json`, `figures/`).

## Results produced by this code

`python main.py` (seed 42, 10 runs, 100 nodes, 40 s), stored in
`results/simulation_results.json`. Mean ± 95 % CI half-width at t = 40 s.

| Variant | PDR | Delay proxy (ms) | Energy (J) | DR (%) | FPR (%) |
|---|---|---|---|---|---|
| full | 0.814 ± 0.029 | 11.23 ± 1.16 | 151.4 ± 12.4 | 100.0 ± 0.0 | 0.0 ± 0.0 |
| no_sliding_window | 0.810 ± 0.030 | 11.24 ± 1.12 | 151.4 ± 12.2 | 100.0 ± 0.0 | 0.0 ± 0.0 |
| fixed_threshold | 0.813 ± 0.029 | 11.24 ± 1.16 | 151.0 ± 12.5 | 91.0 ± 5.3 | 0.0 ± 0.0 |
| no_onoff_detector | 0.814 ± 0.029 | 11.23 ± 1.16 | 151.4 ± 12.4 | 100.0 ± 0.0 | 0.0 ± 0.0 |
| no_collusion_filter | 0.814 ± 0.031 | 11.25 ± 1.12 | 151.3 ± 12.3 | 100.0 ± 0.0 | 0.0 ± 0.0 |
| no_defense | 0.494 ± 0.059 | 8.28 ± 1.17 | 100.5 ± 10.0 | 0.0 | 0.0 |
| no_attack | 0.813 ± 0.029 | 11.34 ± 1.08 | 152.2 ± 12.6 | n/a | 0.0 ± 0.0 |

How to read it:

- The defence restores the attack-free PDR (full − no_defense = +0.319 ± 0.046, paired, p < 10⁻⁶).
- Only the adaptive threshold has a measurable effect (DR −9.0 ± 5.3 points without it, p = 0.004, all of it on on-off nodes).
  Sliding window, on-off detector and collusion filter show no difference in this scenario.
- DR = 100 % and FPR = 0 % is a ceiling effect: honest nodes fail 5 % i.i.d., every attacker forwards on average less than 50 %, and each node is observed by ~15 neighbours every second. The scenario does not discriminate between trust designs; harder scenarios (lower drop rates, correlated link losses, more attackers) are needed before any claim about the individual components.
- The router training fitness saturates (train PDR = 1.0 in 9/10 runs, after 1–29 HLOA iterations): 30 fitness pairs are too few to rank weight vectors.
- No comparison with EER-MANET-EFIAGNN or other published methods is possible from this code: they are not implemented here.

## Structure

```
main.py                      # runs, variants, aggregation
config.py                    # parameters (new ones documented inline)
src/simulation/manet_env.py  # scenario, mobility, watchdog, routing, energy, metrics
src/simulation/behaviour.py  # ground-truth attack model (never seen by the defence)
src/trust/adaptive_trust.py  # trust manager (observation-only) with ablation switches
src/gnn/at_efiagnn.py        # 3-layer GNN (vectorised, same maths)
src/optimization/hloa.py     # HLOA
src/clustering/fcmvc.py      # fuzzy c-means (membership used as a GNN feature)
src/evaluation/metrics.py    # CI, paired comparisons
tests/test_corrected.py
```

## Known limitations (not fixed)

- Abstract simulator, no protocol, no MAC/PHY: delay and overhead cannot be measured.
- Watchdog observations assume promiscuous monitoring of every neighbour every second, at no energy cost.
- The network-wide trust used for flagging is the median of per-observer trust (centralised IDS / ideal consensus assumption).
- Cluster heads are computed but not used by routing.
- 10 malicious nodes per run: detection rate moves in steps of 10 points per run.
