# AODV in a mobile, multi-hop ad hoc network: configuration and performance

## Question

Which AODV configuration gives the best network performance in a dynamic,
infrastructure-less, multi-hop network whose nodes move at 1–10 m/s?
"Best" is ranked by delivery ratio first, then delay, then routing
overhead. The ranking rule was fixed before the campaigns were run.

## Scenario

| Aspect | Setting |
|---|---|
| Nodes, area | 50 nodes, 1000 m × 1000 m |
| Mobility | steady-state random waypoint, speed U[1, 10] m/s, no pause (starts in the stationary regime, so no warm-up transient) |
| Radio | 802.11b ad hoc (IBSS), 11 Mb/s data, 1 Mb/s control and broadcast, unit-disk range 250 m |
| Infrastructure | none: no access point, every node forwards |
| Traffic | 10 CBR UDP flows between random distinct pairs, 4 × 512 B packets/s each (16 kb/s), active from ~10 s to 190 s |
| Duration | 200 s per replication |
| Simulator | ns-3.48, `scenarios/manet_scenario.py` |

### Is it really multi-hop and dynamic?

The check below uses the positions, not any protocol. `analysis/topology_stats.py`
rebuilt the trajectories of replications 11–30 and sampled the unit-disk graph
every second while each flow was active:

| Measure | Value (mean ± 95 % CI, 20 replications) |
|---|---|
| Shortest source–destination path | 2.38 ± 0.15 hops |
| Samples needing ≥ 2 hops | 75.9 % (2: 33.6 %, 3: 26.2 %, 4: 12.6 %, 5: 3.1 %, 6: 0.4 %) |
| Source–destination connectivity | 0.993 ± 0.006 |

Paths change continuously and three packets in four need relays. The
network is almost never partitioned, so any protocol could in principle
deliver about 99 % of the packets. The losses measured below come from
routing and MAC behaviour, not from a lack of paths.

## Method

- **Metrics.**
  - **PDR:** received / offered. The offered count comes from `odr::CbrSource`, which also counts packets the socket refuses.
  - **Goodput:** received payload bits per flow-second, summed over flows.
  - **Delay and jitter:** FlowMonitor, pooled over received packets.
  - **NRL:** routing control transmissions (every hop and every broadcast) per delivered packet, counted by `odr::ControlTrafficMonitor`. FlowMonitor ignores broadcasts and cannot give this figure.
  - **Hops:** FlowMonitor's count, an upper bound for reactive protocols.
- **Pairing.** For a given run, every variant sees the same trajectories and the same flows (common random numbers). Differences are computed replication by replication, with 95 % Student confidence intervals.
- **Stages.**
  1. Baselines on runs 1–10.
  2. One-factor screening of ns-3 AODV attributes on runs 1–10.
  3. Two combinations of the best factor on runs 1–10.
  4. **Confirmation on fresh runs 11–30.** Choosing the best of 13 variants on the same runs overstates its advantage (winner's curse). The confirmation replications took no part in the choice.
  5. A short screening of ODR's own parameters.

## Results

### Confirmation: runs 11–30

| Variant | PDR | Goodput (kb/s) | Delay (ms) | Jitter (ms) | NRL | Control pkts/run |
|---|---|---|---|---|---|---|
| AODV, ns-3 defaults | 0.874 ± 0.019 | 143.3 ± 3.0 | 15.1 ± 3.1 | 14.5 ± 2.5 | 8.32 ± 1.00 | 51 867 |
| **AODV, tuned profile** | **0.912 ± 0.016** | **149.6 ± 2.7** | 22.0 ± 2.3 | 17.6 ± 1.6 | **3.03 ± 0.15** | 19 860 |
| ODR (AODV RFC 3561 core, this module) | 0.952 ± 0.017 | 156.0 ± 2.8 | 22.3 ± 6.2 | 12.8 ± 2.0 | 0.99 ± 0.12 | 6 745 |

Paired differences (20 pairs):

| Comparison | ΔPDR | ΔGoodput (kb/s) | ΔDelay (ms) | ΔNRL |
|---|---|---|---|---|
| tuned − defaults | **+0.038 ± 0.021** | +6.3 ± 3.4 | +6.8 ± 2.0 | **−5.28 ± 0.89** |
| ODR − defaults | **+0.077 ± 0.017** | +12.7 ± 2.8 | +7.2 ± 6.3 | −7.32 ± 0.94 |
| ODR − tuned | **+0.039 ± 0.019** | — | +0.4 ± 5.7 | **−2.04 ± 0.11** |

The tuned profile delivers significantly more (+3.8 points) with 63 % less
control traffic. It pays about 7 ms of extra mean delay: packets now wait for
a reply from the destination instead of an intermediate node. ODR delivers
another 3.9 points at the same delay with a third of the overhead. It ends
4 points below the connectivity ceiling of 0.993.

### Screening: runs 1–10, differences against AODV ns-3 defaults

| Variant (ns-3 AODV) | ΔPDR | ΔDelay (ms) | ΔNRL |
|---|---|---|---|
| HELLO off (link-layer feedback only) | −0.062 ± 0.080 | −2.0 ± 2.0 | −2.98 ± 1.07 |
| ActiveRouteTimeout 6 s | −0.019 ± 0.042 | −3.8 ± 2.3 | −1.90 ± 0.61 |
| ActiveRouteTimeout 10 s | −0.042 ± 0.044 | −5.7 ± 2.9 | −2.54 ± 0.46 |
| DestinationOnly | +0.029 ± 0.045 | +8.4 ± 6.3 | −4.60 ± 0.95 |
| GratuitousReply off | +0.002 ± 0.069 | −5.8 ± 2.7 | −1.89 ± 0.65 |
| TtlStart 3 | −0.005 ± 0.052 | −1.6 ± 2.7 | −0.18 ± 0.91 |
| RreqRetries 4 | +0.003 ± 0.011 | +10.3 ± 17.4 | −0.12 ± 0.14 |
| HelloInterval 2 s | −0.043 ± 0.080 | −5.1 ± 3.2 | −3.23 ± 0.67 |
| HELLO off, ActiveRouteTimeout 10 s | −0.046 ± 0.033 | −4.1 ± 3.6 | −4.64 ± 0.98 |
| HELLO off, neighbor lifetime 5 s | −0.084 ± 0.055 | −3.1 ± 3.5 | −4.94 ± 0.81 |
| HELLO off, neighbor lifetime 5 s, ART 10 s | −0.085 ± 0.049 | −2.7 ± 5.1 | −5.63 ± 0.93 |
| DestinationOnly + HelloInterval 2 s | +0.010 ± 0.041 | +6.4 ± 3.8 | −5.62 ± 0.98 |
| **DestinationOnly + ActiveRouteTimeout 6 s** (selected) | +0.033 ± 0.043 | +6.7 ± 6.8 | −4.95 ± 0.95 |
| ODR | +0.085 ± 0.037 | +5.6 ± 10.2 | −6.89 ± 0.96 |

The selected combination had the highest mean PDR and the lowest variance
among ns-3 AODV variants. Its PDR gain was not significant on 10 pairs; it
became significant on the 20 fresh confirmation pairs.

ODR parameter screening (runs 1–10): none of ActiveRouteTimeout 6 s,
DestinationOnly, TtlStart 3 or RreqRetries 4 changed ODR's PDR significantly
(all |ΔPDR| ≤ 0.018 with CIs containing 0). ODR's defaults stay.

## Recommended configuration

- **With ns-3's AODV:** `--protocol aodv --aodv-profile tuned`, i.e.
  `EnableHello=true`, `DestinationOnly=true`, `ActiveRouteTimeout=6s`,
  `MyRouteTimeout=12s`, `DeletePeriod=30s`, everything else at ns-3
  defaults. It is the best of the configurations tested. Expect +3.8 PDR
  points and −63 % control traffic against the defaults, for about +7 ms
  of delay.
- **With the AODV protocol implemented by this module (ODR):** default
  attributes. It is the best overall: +7.7 PDR points and −88 % control
  traffic against ns-3's AODV defaults. ODR follows the RFC 3561 message
  formats and rules. The README lists how it differs from ns-3's
  implementation.

## Limits of these results

- **One network configuration.** Density, speed range, range model,
  offered load and traffic type are each fixed. At high load, with TCP, or
  with fading channels the ranking may change; none of this was tested.
- **Local search only.** The screening varied one factor at a time around
  ns-3's defaults, then tested two combinations. Interactions between
  factors were barely explored, so a better AODV configuration may exist.
- **Two causes left unexplained.**
  - ODR's advantage over ns-3's AODV is measured, not explained. Its four
    documented deviations were not tested one by one, so none of them can
    be credited with the gain.
  - The poor and highly variable performance of ns-3's AODV without HELLO
    is also unexplained. The tested hypothesis (neighbor entries expiring
    after 2 s and triggering false link breaks) was refuted: lengthening
    the neighbor lifetime made the PDR worse.
- **Optimistic ceiling.** The connectivity ceiling uses a unit-disk graph
  and ignores interference. It is an optimistic bound, not a reachable
  target.
- **Biased hop counts.** The hop counts reported in the tables come from
  FlowMonitor and are biased upward for reactive protocols. The
  topology-based figure (2.38 hops) is the unbiased one.

## Reproduction

```sh
cd ns-3-dev   # with this repository cloned as contrib/odr
./ns3 run "contrib/odr/scenarios/run_campaign.py contrib/odr/scenarios/campaigns/aodv_baselines.json --runs 10 --jobs 4 -- --outdir $PWD/results/stage0"
./ns3 run "contrib/odr/scenarios/run_campaign.py contrib/odr/scenarios/campaigns/aodv_screening.json --runs 10 --jobs 4 -- --outdir $PWD/results/stage1"
./ns3 run "contrib/odr/scenarios/run_campaign.py contrib/odr/scenarios/campaigns/aodv_combinations.json --runs 10 --jobs 4 -- --outdir $PWD/results/stage1b"
./ns3 run "contrib/odr/scenarios/run_campaign.py contrib/odr/scenarios/campaigns/aodv_confirmation.json --runs 20 --first-run 11 --jobs 4 -- --outdir $PWD/results/stage2"
python3 contrib/odr/analysis/analyze_results.py results/stage2 --reference aodv-ns3-default
```

The per-run and summary CSV files behind every table are in
`docs/results/`. The module revision that produced each run is recorded in
its metadata. All campaigns ran on ns-3.48 with the commits of this
repository dated 2026-09-24.
