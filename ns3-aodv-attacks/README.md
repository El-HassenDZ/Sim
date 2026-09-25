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

| Attack | How, without touching src/aodv | Honest limitation |
|---|---|---|
| **Blackhole** | `MaliciousAodv` wraps the node's real AODV; it delegates all control/routing to AODV (so the node keeps a normal table and forwards RREQ), but drops every **transit data** packet it is asked to forward. | Data-plane dropping only. It does **not** forge RREPs with an inflated sequence number to *actively* attract traffic — that needs editing `src/aodv`. The node attracts routes only as far as normal AODV puts it on a path. |
| **Grayhole** | Same wrapper, drops forwarded data with probability `grayholeProb`. | Same as above. |
| **RREQ flood** | `RreqFlooder` app sends UDP to a stream of unassigned addresses, so the attacker's own AODV emits a genuine RREQ per new destination. The scenario raises the attacker's `RreqRateLimit` so the flood is observable. | Induces real RREQs through AODV's state machine; it does not fabricate RREQ packets on the wire. |

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
  malicious-aodv.{h,cc}        # blackhole / grayhole routing wrapper
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
