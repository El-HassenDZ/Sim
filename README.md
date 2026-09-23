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

## Tests

`./test.py -s odr` runs 9 cases: header serialization and sizes, sequence
number wrap-around, routing table update/invalidation/expiry rules, RREQ
duplicate cache, packet queue, and four Wi-Fi scenarios with deterministic
radio range (4-hop expanding ring discovery, intermediate cached reply, MAC
break detection and repair, RERR propagation up to a partitioned source).
