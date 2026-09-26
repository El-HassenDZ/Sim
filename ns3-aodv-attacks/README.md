# ns3-aodv-attacks — AODV under attack in ns-3.48

An ns-3.48 project that simulates an **AODV** MANET in two modes — a clean
**baseline** (no attacker) and an **attacked** run — and writes the metrics,
routing tables, mobility trace and attack log for both. The AODV protocol
itself (`src/aodv`) is **never modified**: the attacks are a routing
*wrapper* and an *application* that live in `scratch/`.

> **Status: statically reviewed, NOT compiled.** ns-3.48 was not available
> to the author, so this code has not been built or run. Treat it as a
> ready-to-build scaffold, compile it in your own ns-3.48 tree, and only
> then cite any number. Version-sensitive lines are marked `NS3-VERSION`.

## What is (and is not) modelled

| Attack (`--attack=`) | How, outside src/aodv | Honest limitation |
|---|---|---|
| **`blackhole`** | `MaliciousAodv` wraps the node's real AODV; it delegates all control/routing to AODV (so the node keeps a normal table and forwards RREQ), but drops every **transit data** packet it is asked to forward. | Data-plane dropping only. It does **not** forge RREPs — the node attracts routes only as far as normal AODV puts it on a path. |
| **`blackhole_rrep`** | **Active blackhole.** `BlackholeAodv` is a self-contained malicious routing agent that runs *instead of* AODV on the attacker. It reuses only the **public** AODV packet headers (`ns3/aodv-packet.h`) to answer every RREQ it hears with a **forged RREP** (destination sequence number ≈ 2³¹, hop count 1), so honest AODV prefers it and routes through it; it then drops all transit data. | A derived agent, **not** a copy of `src/aodv` and not a change to it. It only wins route discovery and drops; it has no RERR/HELLO/expanding-ring logic (a blackhole does not need them). Reviewed statically only. |
| **`grayhole`** | Same wrapper as `blackhole`, drops forwarded data with probability `grayholeProb`. | Data-plane dropping only. |
| **`flood`** | `RreqFlooder` app sends UDP to a stream of unassigned addresses, so the attacker's own AODV emits a genuine RREQ per new destination. The scenario raises the attacker's `RreqRateLimit` so the flood is observable. | Induces real RREQs through AODV's state machine; it does not fabricate RREQ packets on the wire. |

Why the wrapper approach is faithful where it counts: the malicious node
still runs unmodified AODV for everything except the final forwarding
decision, so route discovery, timers, sequence numbers, table expiry and
link breaks behave exactly as ns-3's AODV — only the data it should relay
disappears. That is the defining behaviour of a blackhole/grayhole on the
forwarding path.

## Layout

```
scratch/aodv-attacks/          # dropped into <ns-3.48>/scratch/
  aodv-attack-sim.cc           # main scenario, both modes, all measurements
  malicious-aodv.{h,cc}        # blackhole / grayhole routing wrapper (data-plane)
  blackhole-aodv.{h,cc}        # ACTIVE blackhole: forged-RREP agent (derived, outside src/aodv)
  rreq-flooder.{h,cc}          # RREQ-flooding application
runner/
  config.py                    # scenario parameters (one source of truth)
  run_experiment.py            # build + run baseline & attacks, aggregate, compare
  parse_flowmon.py             # independent cross-check from FlowMonitor XML
outputs/                       # generated
```

## Build & run

```bash
# 1. Point the runner at your ns-3.48 checkout (the one with ./ns3 and src/aodv).
python3 runner/run_experiment.py --ns3-dir /path/to/ns-3.48 --runs 10

# or by hand, one run:
cp -r scratch/aodv-attacks /path/to/ns-3.48/scratch/
cd /path/to/ns-3.48
./ns3 run "aodv-attacks --mode=baseline --out=base"
./ns3 run "aodv-attacks --mode=attack --attack=blackhole --nMalicious=5 --out=bh"
```

`run_experiment.py` runs the baseline and each attack for `--runs`
repetitions **on the same RNG run numbers**, so the baseline and the
attacked run of the same index share topology, mobility and traffic — the
attack is the only difference, which is what makes the comparison paired.

## Output files (per run/mode, prefixed by `--out`)

| File | Contents |
|---|---|
| `*_metrics.csv` | mode, attack, PDR %, throughput kbps, avg delay ms, control packets/bytes, **normalised routing overhead** (control pkts / delivered data pkts), energy consumed (total and per node), attacker drop count, flood probes |
| `*_routing.txt` | every node's AODV routing table at 25 %, 50 %, 90 % of sim time (`PrintRoutingTableAllAt`) |
| `*_mobility.csv` | `time,node,x,y,speed` sampled every 1 s |
| `*_attacks.csv` | one row per attacker: node id, type, start time, dropped packets/bytes, flood probes (baseline writes a "no attacker" line) |
| `*_flowmon.xml` | raw FlowMonitor dump for audit / `parse_flowmon.py` |

## Metric definitions (state these in any write-up)

- **PDR** = received data packets / sent data packets, over the CBR flows only (UDP dest port 8000). AODV control and flooder probes are excluded from the numerator and denominator.
- **Throughput** = received data bits / (last − first data-rx time), in kbps.
- **Delay** = mean end-to-end delay of received data packets, in ms.
- **Normalised routing overhead** = AODV control packets (UDP port 654, counted at `Ipv4L3Protocol::Tx` on all nodes) / delivered data packets. Unitless.
- **Energy** = Σ (initial − remaining) over `BasicEnergySource`, from the `WifiRadioEnergyModel`, in Joules.

## Baseline tuning for high attack-free PDR

The baseline parameters were chosen to remove the known sinks of AODV PDR.
Each is a mechanism, not a measured value — **the author cannot run ns-3.48,
so run the scenario and confirm the numbers.**

| Lever | Value | Why it raises attack-free PDR |
|---|---|---|
| Propagation | `range` (hard disk, `commRange`=250 m) | Fixes the transmission range deterministically, so connectivity is exactly what `check_connectivity.py` validates — no txPower/fading guesswork. |
| Field / density | 50 nodes in 800×800 m | Mean degree ≈ 11 at 250 m, **98.6 % of random placements fully connected** (verified). No partition = no unavoidable losses. |
| Path length | 800×800 (was 1000×1000) | Shorter routes (≈2–4 hops) → less per-hop loss and lower delay. |
| Mobility | 1–3 m/s, 10 s pause (was 1–5, 2 s) | Fewer link breaks → fewer route-rediscovery gaps where packets drop. |
| Load | 10 × 16 kbps, 512 B | 160 kbps on an 11 Mbps channel → negligible MAC contention / queue overflow. |

**Connectivity is verified without ns-3.** `runner/check_connectivity.py`
builds the unit-disk graph (identical to `RangePropagationLossModel`) over
hundreds of random placements and reports the fraction connected:

```
$ python3 runner/check_connectivity.py
 range (m) |  mean degree |  fully connected | same-component pairs
       200 |          7.6 |            80.2% |                98.3%
       250 |         11.2 |            98.6% |                99.9%
```

At the configured 250 m the field is essentially never partitioned, so the
attack-free PDR is bounded by mobility and load (both kept mild), not by
topology. Switch `--propagation=logdistance` to test robustness under a
realistic, range-varying channel (expect a lower, fading-dependent PDR).

This is **standard AODV under good conditions, not a modified AODV.**

## On the objective "best performance in the baseline mode"

The baseline is plain AODV in a **deliberately healthy** setting (50 nodes
in 1000×1000 m, 16 dBm ≈ 250 m range, moderate 1–5 m/s mobility, 10 CBR
flows). That gives AODV a high attack-free PDR to serve as the reference.
This is **standard AODV under good conditions, not an improved AODV**: the
project measures how much each attack degrades that reference, it does not
add a defence. If the goal is to *beat* standard AODV, that requires a
routing change or a defence layer — a separate piece of work, and one that
would mean touching or extending `src/aodv`.

## What still needs your machine

- Compile in ns-3.48 and fix any `NS3-VERSION` spot the build flags (most
  likely the `ns3::energy` namespace and `RreqRateLimit` attribute path).
- Confirm the baseline PDR is high (healthy topology) before trusting the
  attack deltas; if the baseline itself is partitioned, raise `txPower` or
  node density.
- Increase `--runs` until the 95 % CIs are tight enough for your claim.
