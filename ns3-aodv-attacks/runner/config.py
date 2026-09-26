"""
runner/config.py
================
Scenario parameters for the AODV attack experiments. Every value here is
passed to the ns-3 scratch program as a --key=value argument, so the C++
defaults and these stay in sync from one place.

The baseline is tuned to be a HEALTHY network (dense enough, adequate Tx
power, moderate mobility) so that attack-free AODV already delivers well —
that is the reference the attacked runs are measured against. It is not a
claim of beating standard AODV; it is standard AODV under good conditions.
"""

# ns-3 scratch target name (the folder name under scratch/).
TARGET = "aodv-attacks"

# Repetitions per (mode, attack): independent RNG runs for confidence
# intervals.
N_RUNS = 10

# Scenario (keys must match cmd.AddValue names in aodv-attack-sim.cc).
SCENARIO = {
    "nNodes": 50,          # density: ~7-10 neighbours in range (well above the
                           #   connectivity threshold ln(N)~3.9, so no partition)
    "simTime": 100.0,
    "areaX": 800.0,        # was 1000: smaller field -> shorter paths (fewer hops
    "areaY": 800.0,        #   -> higher PDR, lower delay) while staying multi-hop
    "minSpeed": 1.0,
    "maxSpeed": 3.0,       # was 5: slower nodes -> fewer link breaks -> higher PDR
    "pause": 10.0,         # was 2: longer pauses -> more stable topology
    "nFlows": 10,          # 10 x 16 kbps = 160 kbps offered on an 11 Mbps channel
    "dataRate": "16kbps",  #   -> light load, negligible MAC contention
    "packetSize": 512,
    "txPower": 18.0,       # dBm; feeds the energy model
    "propagation": "range",# 'range' = deterministic disk of radius commRange, so
                           #   connectivity == the graph check_connectivity.py
                           #   validates; 'logdistance' for realism (range varies)
    "commRange": 250.0,    # m; 50 nodes / 800x800 at 250 m = 98.6% connected
                           #   (verified by runner/check_connectivity.py)
    "initEnergy": 100.0,   # J
    "attackStart": 20.0,   # attacks off for the first 20 s (baseline warm-up)
}

# Why these raise the attack-free PDR (mechanism, not a measured value — the
# author cannot run ns-3.48, so run it and confirm):
#   connectivity (density, txPower) : avoids partitions and route-discovery
#                                     failures, the biggest PDR sink;
#   short paths (smaller area)      : fewer hops -> less per-hop loss and delay;
#   low mobility (maxSpeed, pause)  : fewer route breaks -> fewer rediscovery
#                                     gaps where packets are dropped/buffered;
#   light load (flows, rate)        : negligible collisions and queue overflow.
# It stays a genuine mobile multi-hop MANET; it is standard AODV under good
# conditions, not a modified/improved AODV.

# Attack configuration for the attacked runs.
#   blackhole      : data-plane dropping (routing wrapper, malicious-aodv)
#   blackhole_rrep : ACTIVE blackhole that forges RREPs to attract traffic,
#                    then drops it (derived agent, blackhole-aodv)
#   grayhole       : probabilistic data-plane dropping
#   flood          : RREQ flooding
#   mixed          : blackhole + grayhole + flood spread over the attackers
ATTACKS = ["blackhole", "blackhole_rrep", "grayhole", "flood", "mixed"]
N_MALICIOUS = 5            # 10 % of 50 nodes
GRAYHOLE_PROB = 0.5

# Metric columns considered "higher is better" / "lower is better" for the
# comparison print-out.
HIGHER_BETTER = {"pdr_percent", "throughput_kbps"}
LOWER_BETTER = {"avg_delay_ms", "norm_routing_overhead", "energy_consumed_J"}
