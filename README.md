# ODR — On-Demand Routing for ns-3

`odr` is an ns-3 contributed module implementing a reactive, hop-count routing
protocol for IPv4 mobile ad hoc networks. Version 1 deliberately reproduces
the RFC 3561 core so that it can be validated against ns-3's AODV before any
new route selection metric is introduced.

Tested with ns-3.48 (C++23, CMake + Ninja, cppyy 3.5.0 Python bindings).

## Installation

The repository root is the module. Clone it into `contrib/` under the name
`odr`, then enable it:

```sh
cd ns-3-dev
git clone <this repository> contrib/odr
./ns3 configure --enable-python-bindings --enable-tests \
    --enable-modules "core;network;internet;wifi;mobility;applications;flow-monitor;aodv;odr"
./ns3 build
./test.py -s odr
```

## Usage

C++:

```cpp
OdrHelper odr;
odr.Set("ActiveRouteTimeout", TimeValue(Seconds(3)));
InternetStackHelper stack;
stack.SetRoutingHelper(odr);
stack.Install(nodes);
odr.AssignStreams(nodes, 0);
// ...
Simulator::Run();
OdrHelper::WriteStatistics(nodes, "odr-stats.csv");
Simulator::Destroy();
```

Python (`from ns import ns`): the same calls through `ns.OdrHelper`, e.g.
`ns.OdrHelper.WriteStatistics(nodes, "odr-stats.csv")`. The protocol object is
`ns.odr.RoutingProtocol`, its counters `ns.odr.Statistics`.

`WriteStatistics` must run after `Simulator::Run()` and before
`Simulator::Destroy()`. No Python code runs during the simulation.

## Protocol scope (v1)

Implemented:

- expanding-ring RREQ flooding (TTL 1, 3, 5, 7, then `NetDiameter`) with
  binary exponential back-off at full TTL, per-second rate limiting and
  uniform rebroadcast jitter;
- destination sequence numbers with 32-bit wrap-around comparison;
- replies from the destination or from an intermediate node holding a fresh
  route (disable with `DestinationOnly`);
- link break detection from 802.11 retry exhaustion only, RERR propagation
  along precursor lists, RERR on transit packets without a route;
- bounded queue (length and delay) for packets awaiting discovery.

Not implemented, on purpose: HELLO beacons, local repair, gratuitous RREP,
RREP-ACK blacklisting of unidirectional links, forwarding of broadcast data.
Without HELLO, a non-Wi-Fi device gives no link feedback and broken routes
only disappear when they expire.

Deviations from ns-3's AODV worth knowing when comparing the two:

| Behaviour | ns-3 AODV | ODR |
|---|---|---|
| MAC drop treated as link break | any `DroppedMpdu` reason | `WIFI_MAC_DROP_REACHED_RETRY_LIMIT` only |
| Sequence number taken from a RERR | copied verbatim | fresher of the two (never regresses) |
| Cached reply whose next hop is the requester | allowed | refused |
| Derived timers (`NetTraversalTime`, ...) | independent attributes | recomputed from base attributes |
| HELLO | enabled by default (`EnableHello=true`) | absent |

For an overhead comparison that isolates route discovery, run AODV with
`ns3::aodv::RoutingProtocol::EnableHello=false`; with its default, AODV pays
a constant beaconing cost that ODR does not.

## Attributes

| Attribute | Default | Meaning |
|---|---|---|
| `ActiveRouteTimeout` | 3 s | lifetime granted to a route each time it carries data |
| `NodeTraversalTime` | 40 ms | per-hop traversal estimate; drives all derived timers |
| `NetDiameter` | 35 | maximum path length; TTL of full floods |
| `RreqRetries` | 2 | extra full-TTL floods before giving up |
| `RreqRateLimit` / `RerrRateLimit` | 10 /s | control message rate limits |
| `TtlStart` / `TtlIncrement` / `TtlThreshold` | 1 / 2 / 7 | expanding ring search |
| `TimeoutBuffer` | 2 | slack in ring timeouts, in hops |
| `MaxQueueLength` / `MaxQueueTime` | 64 / 30 s | packets awaiting a route |
| `DestinationOnly` | false | forbid intermediate replies |
| `MaxJitter` | 10 ms | upper bound of rebroadcast jitter |

Derived constants: `NetTraversalTime = 2·NodeTraversalTime·NetDiameter`,
`PathDiscoveryTime = 2·NetTraversalTime`, `DeletePeriod = 5·ActiveRouteTimeout`.

## Exported counters

`odr-stats.csv` holds one row per node. Message counters count transmissions
(one per interface for broadcasts). Byte counters cover the ODR payload only
(RREQ 24 B, RREP 20 B, RERR 4 + 8n B); add 28 B per packet for IPv4 + UDP.

| Column | Content |
|---|---|
| `rreq_originated`, `rreq_forwarded` | RREQ floods started (retries included), rebroadcasts |
| `rrep_originated`, `rrep_forwarded` | replies sent as destination or from cache, relayed |
| `rerr_sent`, `rerr_suppressed` | RERR transmitted, withheld by the rate limiter |
| `control_tx_*`, `control_rx_*` | all control packets and bytes |
| `discoveries_*`, `discovery_latency_sum_s` | discovery outcomes; mean latency = sum / succeeded |
| `link_breaks` | MAC-detected losses that invalidated routes |
| `data_dropped_*` | routing-layer data drops by cause |
| `data_queued_at_end` | packets still waiting for a route when the run stopped |

Normalized routing load = Σ `control_tx_packets` / delivered data packets
(from FlowMonitor). The packet balance closes as
sent = delivered + routing drops + MAC/PHY losses + `data_queued_at_end` + in flight.

## Scenario script

`scenarios/manet_scenario.py` runs one replication of a MANET scenario with
ODR, AODV, OLSR or DSDV and writes FlowMonitor XML, per-node routing control
counts, the ODR counters (ODR runs) and a JSON metadata file:

```sh
./ns3 run "contrib/odr/scenarios/manet_scenario.py --protocol odr --nodes 50 --run 1"
./ns3 run "contrib/odr/scenarios/manet_scenario.py --help"
```

Defaults: 50 nodes in 1000 m × 1000 m, steady-state random waypoint at
1–10 m/s without pause, 802.11b at 11 Mb/s, unit-disk range of 250 m,
10 CBR flows of 4 × 512-byte packets per second, 200 s runs with traffic
between 10 s and 190 s, AODV HELLO disabled.

For a given seed and run, every protocol sees the same trajectories and the
same flows (fixed random stream blocks per subsystem; traffic matrix drawn
from seed and run only). The metadata `config_id` fingerprints the network
configuration, so `(config_id, seed, run)` pairs protocols replication by
replication.

Measurement notes, each verified on this code base:

- **Offered load.** Data sources are `odr::CbrSource`, not
  `OnOffApplication`. When OLSR or DSDV have no route, the socket refuses
  the packet; `OnOffApplication` skips it silently and FlowMonitor never
  sees it. On a sparse 20-node test run, FlowMonitor alone reported a PDR of
  0.81 for OLSR where the delivered-over-offered ratio was 0.10. Use the
  per-flow `attempted` count from the metadata as the PDR denominator;
  `accepted` equals FlowMonitor's `txPackets`.
- **Loss.** Compute losses as offered − received, not with FlowMonitor's
  `lostPackets`: packets still parked by a routing protocol at the end of the
  run are in neither count.
- **Hop count.** FlowMonitor's `timesForwarded` over-counts by one every
  packet a reactive protocol parked during a discovery (it re-enters IP
  through the loopback device). On a 2-hop chain: ODR 2.06, AODV 2.02,
  OLSR 2.00. Report hop counts from reactive protocols with that caveat, or
  not at all.
- **Control overhead.** FlowMonitor ignores every non-unicast packet
  (`Ipv4FlowProbe` returns early on broadcasts), so RREQ floods, HELLO
  beacons and broadcast RERRs never appear in its statistics. The scenario
  counts control traffic with `odr::ControlTrafficMonitor` on the IPv4 `Tx`
  trace, per transmission and per hop, on the protocol's UDP port, and writes
  it to `<tag>.control.csv`. On ODR it matches the protocol's own counters
  exactly (unit test).

## Campaigns and analysis

```sh
# 10 paired replications of each variant listed in the JSON file, 4 in parallel
./ns3 run "contrib/odr/scenarios/run_campaign.py \
    contrib/odr/scenarios/campaigns/aodv_baselines.json --runs 10 --jobs 4 \
    -- --outdir /abs/path/results/stage0"

# Per-run metrics, 95 % CIs per variant, paired differences against a reference
python3 contrib/odr/analysis/analyze_results.py /abs/path/results/stage0 \
    --reference aodv-ns3-default
```

A variant is a label, a protocol and extra scenario arguments (typically
`--set ns3::aodv::RoutingProtocol::<Attribute>=<value>`). Arguments after
`--` are shared by all variants, so their replications pair up. Re-running
the same command resumes an interrupted campaign. The analysis uses only the
Python standard library and writes `runs.csv`, `summary.csv` and
`paired.csv`.

## Tests

`./test.py -s odr` runs 10 cases: header serialization and sizes, sequence
number wrap-around, routing table update/invalidation/expiry rules, RREQ
duplicate cache, packet queue, four Wi-Fi scenarios with deterministic radio
range (4-hop expanding ring discovery, intermediate cached reply, MAC break
detection and repair, RERR propagation up to a partitioned source), and
`CbrSource` accounting of socket refusals.
