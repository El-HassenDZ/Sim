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
    "nNodes": 50,
    "simTime": 100.0,
    "areaX": 1000.0,
    "areaY": 1000.0,
    "minSpeed": 1.0,
    "maxSpeed": 5.0,
    "pause": 2.0,
    "nFlows": 10,
    "dataRate": "16kbps",
    "packetSize": 512,
    "txPower": 16.0,       # dBm; ~250 m range with 802.11b defaults
    "initEnergy": 100.0,   # J
    "attackStart": 20.0,
}

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
