"""Mobile ad hoc network scenario for comparing ODR with ns-3 baselines.

One invocation simulates one replication: a set of nodes moving in a
rectangle, communicating over 802.11b ad hoc, routed by ODR, AODV, OLSR or
DSDV, and carrying constant-bit-rate UDP flows between random node pairs.

Modelling assumptions
---------------------
* Mobility: steady-state random waypoint by default. The classic random
  waypoint model starts from a uniform node distribution that it then drifts
  away from, and its average speed decays over time when the minimum speed is
  close to zero (Yoon, Liu and Noble, INFOCOM 2003). Starting directly in the
  stationary regime (Navidi and Camp, 2004) removes that transient, so the
  whole run is usable. The classic model and static placement remain
  available for comparison with older results.
* Radio: 802.11b at a constant 11 Mb/s data rate, so that route quality
  cannot act through rate adaptation. The default "range" loss model is a unit
  disk (reception is certain within --range, impossible beyond), which is
  optimistic but makes connectivity a pure function of distance; "logdistance"
  uses ns-3's default log-distance model instead and ignores --range.
* Traffic: --flows CBR UDP flows between distinct random (source,
  destination) pairs, each on its own destination port. Flows start at a
  random instant in [warmup, warmup + 1 s) to avoid synchronized discoveries
  and stop --drain seconds before the end so that packets in flight can still
  arrive. Sources are odr::CbrSource rather than OnOffApplication: when a
  proactive protocol has no route the socket refuses the packet, which
  OnOffApplication silently skips and FlowMonitor never sees. CbrSource counts
  it, so the offered load (attempted packets) is the same for every protocol.
* Randomness: RngSeedManager seed and run are taken from --seed and --run.
  Random streams are assigned in fixed, disjoint blocks per subsystem, and the
  traffic matrix is drawn from (seed, run) only. For a given (seed, run), every
  protocol therefore sees the same trajectories and the same flows (common
  random numbers), which allows paired comparisons between protocols.

Outputs (in --outdir, prefixed by a tag built from the main parameters)
-----------------------------------------------------------------------
* <tag>.flowmon.xml: ns-3 FlowMonitor statistics for every IPv4 flow,
  routing control flows included; data flows are identified by the ports
  listed in the metadata.
* <tag>.control.csv: per-node routing control transmissions (packets and
  IPv4 bytes), counted by odr::ControlTrafficMonitor on the protocol's UDP
  port. FlowMonitor cannot provide this figure: it ignores broadcasts, i.e.
  most of the control traffic of every MANET protocol.
* <tag>.odr.csv: per-node ODR counters (ODR runs only), written by
  OdrHelper::WriteStatistics.
* <tag>.meta.json: parameters, network configuration fingerprint (identical
  across protocols and replications of the same network), protocol variant
  (protocol plus a fingerprint of --aodv-hello and --set), data flows with
  their attempted, accepted and refused packet counts, port of the routing
  control traffic, module revision and wall-clock duration. Use "attempted"
  as the delivery ratio denominator.
* <tag>.mobility.txt (--mobility-trace) and <tag>-*.pcap (--pcap).

No Python code runs while the simulation executes: every measurement is
collected by C++ objects and read or serialized after Simulator::Run().

Example
-------
    ./ns3 run "contrib/odr/scenarios/manet_scenario.py --protocol odr --run 3"
"""

from __future__ import annotations

import argparse
import hashlib
import ipaddress
import json
import random
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Sequence

try:
    from ns import ns
except ModuleNotFoundError:
    raise SystemExit(
        "ns-3 Python bindings not found: configure ns-3 with --enable-python-bindings "
        "and run this script through ./ns3 run"
    )

PROTOCOLS = ("odr", "aodv", "olsr", "dsdv")

# UDP ports of routing control traffic, counted by ControlTrafficMonitor and
# needed by the analysis to separate FlowMonitor's control flows from data.
CONTROL_PORTS = {"odr": 5654, "aodv": 654, "olsr": 698, "dsdv": 269}

DATA_BASE_PORT = 10000

# Each subsystem draws from its own block of random streams. Blocks are wide
# enough for several hundred nodes and fixed across protocols, so that adding
# or changing the routing protocol never shifts the streams of mobility.
STREAM_BLOCK = 10000
STREAM_MOBILITY = 0
STREAM_WIFI = 1 * STREAM_BLOCK
STREAM_INTERNET = 2 * STREAM_BLOCK
STREAM_ROUTING = 3 * STREAM_BLOCK


@dataclass
class FlowSpec:
    """A CBR data flow and, once the run is over, its source counters."""

    flow_index: int
    src_node: int
    dst_node: int
    src_addr: str
    dst_addr: str
    dst_port: int
    start_s: float
    stop_s: float
    attempted: int = 0
    accepted: int = 0
    refused: int = 0


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    """Parse and validate the command line.

    Args:
        argv: Command-line arguments, program name excluded.

    Returns:
        The validated arguments.
    """
    parser = argparse.ArgumentParser(
        description="Simulate one replication of a MANET routing scenario.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--protocol", choices=PROTOCOLS, default="odr")
    parser.add_argument("--nodes", type=int, default=50)
    parser.add_argument("--area-x", type=float, default=1000.0, help="Width (m)")
    parser.add_argument("--area-y", type=float, default=1000.0, help="Height (m)")
    parser.add_argument(
        "--mobility",
        choices=("rwp-steady", "rwp", "static"),
        default="rwp-steady",
    )
    parser.add_argument("--speed-min", type=float, default=1.0, help="m/s")
    parser.add_argument("--speed-max", type=float, default=10.0, help="m/s")
    parser.add_argument("--pause", type=float, default=0.0, help="Pause (s)")
    parser.add_argument("--loss", choices=("range", "logdistance"), default="range")
    parser.add_argument("--range", type=float, default=250.0, help="Unit-disk range (m)")
    parser.add_argument("--flows", type=int, default=10)
    parser.add_argument("--packet-size", type=int, default=512, help="UDP payload (B)")
    parser.add_argument("--rate", type=float, default=4.0, help="Packets per second per flow")
    parser.add_argument("--sim-time", type=float, default=200.0, help="s")
    parser.add_argument(
        "--warmup",
        type=float,
        default=10.0,
        help="Traffic starts after this time; lets proactive protocols converge (s)",
    )
    parser.add_argument(
        "--drain",
        type=float,
        default=10.0,
        help="Traffic stops this long before the end of the run (s)",
    )
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--run", type=int, default=1)
    parser.add_argument(
        "--aodv-hello",
        action="store_true",
        help="Keep AODV HELLO beacons (ns-3 default); off by default for a like-for-like "
        "comparison with ODR, which has none",
    )
    parser.add_argument(
        "--set",
        action="append",
        default=[],
        metavar="NAME=VALUE",
        help="Override an ns-3 attribute default, e.g. "
        "ns3::odr::RoutingProtocol::ActiveRouteTimeout=5s (repeatable)",
    )
    parser.add_argument(
        "--label",
        default="",
        help="Human-readable name of the variant, stored in the metadata for the analysis",
    )
    parser.add_argument("--outdir", type=Path, default=Path("results"))
    parser.add_argument("--mobility-trace", action="store_true")
    parser.add_argument(
        "--skip-existing",
        action="store_true",
        help="Do nothing if this replication's metadata already exists (campaign resume)",
    )
    parser.add_argument("--pcap", action="store_true")
    args = parser.parse_args(argv)

    if args.nodes < 2:
        parser.error("--nodes must be at least 2")
    if not 1 <= args.flows <= args.nodes * (args.nodes - 1):
        parser.error("--flows must be between 1 and nodes * (nodes - 1)")
    if args.area_x <= 0 or args.area_y <= 0 or args.range <= 0:
        parser.error("--area-x, --area-y and --range must be positive")
    if args.mobility != "static":
        # A zero minimum speed lets random waypoint nodes get stuck on
        # arbitrarily long legs, and the steady-state model rejects it outright.
        if not 0 < args.speed_min <= args.speed_max:
            parser.error("mobile scenarios need 0 < --speed-min <= --speed-max")
    if args.pause < 0:
        parser.error("--pause must be non-negative")
    if args.packet_size <= 0 or args.rate <= 0:
        parser.error("--packet-size and --rate must be positive")
    if args.warmup < 0 or args.drain < 0:
        parser.error("--warmup and --drain must be non-negative")
    if args.sim_time <= args.warmup + 1.0 + args.drain:
        parser.error("--sim-time must exceed warmup + 1 s + drain")
    for override in args.set:
        if "=" not in override:
            parser.error(f"--set expects NAME=VALUE, got {override!r}")
    return args


# Parameters that describe the routing variant or the replication rather than
# the network, or that only control outputs. --set overrides count as routing
# variant: they are meant for protocol attributes, and a campaign that changes
# network attributes through --set must use its own output directory.
_NON_NETWORK_PARAMETERS = (
    "protocol",
    "aodv_hello",
    "set",
    "label",
    "seed",
    "run",
    "outdir",
    "mobility_trace",
    "pcap",
    "skip_existing",
)


def _fingerprint(values: dict) -> str:
    """Hash a JSON-serializable mapping into eight hexadecimal digits."""
    digest = hashlib.sha256(json.dumps(values, sort_keys=True).encode("utf-8"))
    return digest.hexdigest()[:8]


def config_id(args: argparse.Namespace) -> str:
    """Fingerprint the network configuration: topology, mobility, radio, traffic.

    Runs sharing this identifier and the same (seed, run) saw the same
    trajectories and flows, whatever the protocol: this triple is the key that
    pairs protocols replication by replication in the analysis.

    Args:
        args: Scenario arguments.

    Returns:
        Eight hexadecimal digits.
    """
    return _fingerprint(
        {key: value for key, value in vars(args).items() if key not in _NON_NETWORK_PARAMETERS}
    )


def protocol_variant(args: argparse.Namespace) -> str:
    """Name the routing protocol together with its non-default settings.

    Args:
        args: Scenario arguments.

    Returns:
        The protocol name, suffixed with a fingerprint of its settings when
        they differ from the defaults, e.g. ``odr`` or ``aodv-5c1e09aa``.
    """
    settings = {"set": sorted(args.set)}
    if args.protocol == "aodv" and args.aodv_hello:
        settings["aodv_hello"] = True
    if settings == {"set": []}:
        return args.protocol
    return f"{args.protocol}-{_fingerprint(settings)}"


def scenario_tag(args: argparse.Namespace) -> str:
    """Build the file prefix identifying one replication.

    The readable part names the usual sweep factors; the fingerprints
    guarantee that runs differing in any other parameter never overwrite each
    other.

    Args:
        args: Scenario arguments.

    Returns:
        A tag such as ``odr_n50_v10_p0_f10_3fa2c01b_s1_r1``.
    """
    speed = 0 if args.mobility == "static" else args.speed_max
    return (
        f"{protocol_variant(args)}_n{args.nodes}_v{speed:g}_p{args.pause:g}_f{args.flows}"
        f"_{config_id(args)}_s{args.seed}_r{args.run}"
    )


def apply_attribute_defaults(args: argparse.Namespace) -> None:
    """Install attribute defaults before any ns-3 object is created.

    Args:
        args: Scenario arguments.
    """
    ns.Config.SetDefault(
        "ns3::aodv::RoutingProtocol::EnableHello", ns.BooleanValue(args.aodv_hello)
    )
    for override in args.set:
        name, value = override.split("=", 1)
        ns.Config.SetDefault(name, ns.StringValue(value))


def install_mobility(nodes: "ns.NodeContainer", args: argparse.Namespace) -> None:
    """Install the mobility model and fix its random streams.

    Args:
        nodes: All nodes of the scenario.
        args: Scenario arguments.
    """
    mobility = ns.MobilityHelper()
    x_range = f"ns3::UniformRandomVariable[Min=0.0|Max={args.area_x}]"
    y_range = f"ns3::UniformRandomVariable[Min=0.0|Max={args.area_y}]"
    allocator = ns.CreateObject[ns.RandomRectanglePositionAllocator]()
    allocator.SetAttribute("X", ns.StringValue(x_range))
    allocator.SetAttribute("Y", ns.StringValue(y_range))

    if args.mobility == "rwp-steady":
        # The model draws its own initial positions from the stationary
        # distribution; the allocator only feeds the helper's placement,
        # which the model overrides at initialization.
        mobility.SetMobilityModel(
            "ns3::SteadyStateRandomWaypointMobilityModel",
            "MinSpeed",
            ns.DoubleValue(args.speed_min),
            "MaxSpeed",
            ns.DoubleValue(args.speed_max),
            "MinPause",
            ns.DoubleValue(args.pause),
            "MaxPause",
            ns.DoubleValue(args.pause),
            "MinX",
            ns.DoubleValue(0.0),
            "MaxX",
            ns.DoubleValue(args.area_x),
            "MinY",
            ns.DoubleValue(0.0),
            "MaxY",
            ns.DoubleValue(args.area_y),
        )
    elif args.mobility == "rwp":
        mobility.SetMobilityModel(
            "ns3::RandomWaypointMobilityModel",
            "Speed",
            ns.StringValue(
                f"ns3::UniformRandomVariable[Min={args.speed_min}|Max={args.speed_max}]"
            ),
            "Pause",
            ns.StringValue(f"ns3::ConstantRandomVariable[Constant={args.pause}]"),
            "PositionAllocator",
            ns.PointerValue(allocator),
        )
    else:
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel")
    mobility.SetPositionAllocator(allocator)
    mobility.Install(nodes)

    # The allocator's variables are not reached by MobilityHelper's stream
    # assignment; without their own streams, initial positions would shift
    # whenever another model draws a random number first.
    used = allocator.AssignStreams(STREAM_MOBILITY)
    used += mobility.AssignStreams(nodes, STREAM_MOBILITY + used)
    check_stream_block("mobility", used)


def install_wifi(nodes: "ns.NodeContainer", args: argparse.Namespace):
    """Install 802.11b ad hoc devices on every node.

    Args:
        nodes: All nodes of the scenario.
        args: Scenario arguments.

    Returns:
        A tuple (devices, phy helper); the helper is needed for pcap tracing.
    """
    channel = ns.YansWifiChannelHelper()
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel")
    if args.loss == "range":
        channel.AddPropagationLoss(
            "ns3::RangePropagationLossModel", "MaxRange", ns.DoubleValue(args.range)
        )
    else:
        channel.AddPropagationLoss("ns3::LogDistancePropagationLossModel")

    phy = ns.YansWifiPhyHelper()
    phy.SetChannel(channel.Create())
    mac = ns.WifiMacHelper()
    mac.SetType("ns3::AdhocWifiMac")
    wifi = ns.WifiHelper()
    wifi.SetStandard(ns.WIFI_STANDARD_80211b)
    wifi.SetRemoteStationManager(
        "ns3::ConstantRateWifiManager",
        "DataMode",
        ns.StringValue("DsssRate11Mbps"),
        "ControlMode",
        ns.StringValue("DsssRate1Mbps"),
    )
    devices = wifi.Install(phy, mac, nodes)
    check_stream_block("wifi", ns.WifiHelper.AssignStreams(devices, STREAM_WIFI))
    return devices, phy


def install_internet(nodes: "ns.NodeContainer", protocol: str) -> None:
    """Install the IPv4 stack with the requested routing protocol.

    Args:
        nodes: All nodes of the scenario.
        protocol: One of PROTOCOLS.
    """
    helpers = {
        "odr": ns.OdrHelper,
        "aodv": ns.AodvHelper,
        "olsr": ns.OlsrHelper,
        "dsdv": ns.DsdvHelper,
    }
    routing = helpers[protocol]()
    stack = ns.InternetStackHelper()
    stack.SetRoutingHelper(routing)
    stack.Install(nodes)
    check_stream_block("internet", stack.AssignStreams(nodes, STREAM_INTERNET))
    # DsdvHelper offers no AssignStreams: DSDV runs stay reproducible for a
    # given command line, but its jitter streams are not pinned like the
    # other protocols'.
    if protocol != "dsdv":
        check_stream_block("routing", routing.AssignStreams(nodes, STREAM_ROUTING))


def ipv4_to_str(address: "ns.Ipv4Address") -> str:
    """Render an ns-3 IPv4 address in dotted notation.

    Args:
        address: The address.

    Returns:
        The dotted-quad string.
    """
    return str(ipaddress.IPv4Address(address.Get()))


def install_traffic(
    nodes: "ns.NodeContainer",
    interfaces: "ns.Ipv4InterfaceContainer",
    args: argparse.Namespace,
) -> list[tuple[FlowSpec, "ns.odr.CbrSource"]]:
    """Create the CBR flows and their sinks.

    The traffic matrix is drawn with a Python generator seeded from (seed,
    run) alone, so it does not depend on the routing protocol or on how many
    ns-3 random variables were created before. CbrSource draws no random
    number, so the traffic needs no stream block of its own.

    Args:
        nodes: All nodes of the scenario.
        interfaces: IPv4 interfaces, in node order.
        args: Scenario arguments.

    Returns:
        Each flow with its source application, in creation order; the
        sources are read back after the run.
    """
    rng = random.Random(args.seed * 1_000_003 + args.run)
    pairs = [(s, d) for s in range(args.nodes) for d in range(args.nodes) if s != d]
    chosen = rng.sample(pairs, args.flows)
    stop_s = args.sim_time - args.drain

    flows = []
    for index, (src, dst) in enumerate(chosen):
        port = DATA_BASE_PORT + index
        start_s = args.warmup + rng.random()

        sink = ns.PacketSinkHelper(
            "ns3::UdpSocketFactory",
            ns.InetSocketAddress(ns.Ipv4Address.GetAny(), port).ConvertTo(),
        )
        sink_apps = sink.Install(nodes.Get(dst))
        sink_apps.Start(ns.Seconds(0.0))
        sink_apps.Stop(ns.Seconds(args.sim_time))

        source = ns.CreateObject[ns.odr.CbrSource]()
        source.SetAttribute(
            "Remote",
            ns.AddressValue(ns.InetSocketAddress(interfaces.GetAddress(dst), port).ConvertTo()),
        )
        source.SetAttribute("PacketSize", ns.UintegerValue(args.packet_size))
        source.SetAttribute("Interval", ns.TimeValue(ns.Seconds(1.0 / args.rate)))
        nodes.Get(src).AddApplication(source)
        source.SetStartTime(ns.Seconds(start_s))
        source.SetStopTime(ns.Seconds(stop_s))

        spec = FlowSpec(
            flow_index=index,
            src_node=src,
            dst_node=dst,
            src_addr=ipv4_to_str(interfaces.GetAddress(src)),
            dst_addr=ipv4_to_str(interfaces.GetAddress(dst)),
            dst_port=port,
            start_s=start_s,
            stop_s=stop_s,
        )
        flows.append((spec, source))
    return flows


def check_stream_block(subsystem: str, used: int) -> None:
    """Abort if a subsystem consumed more random streams than its block holds.

    Overflowing into the next block would silently correlate two subsystems
    and break the common-random-numbers guarantee across protocols.

    Args:
        subsystem: Name used in the error message.
        used: Number of streams consumed.
    """
    if used >= STREAM_BLOCK:
        raise SystemExit(
            f"{subsystem} consumed {used} random streams, more than the "
            f"{STREAM_BLOCK} reserved; increase STREAM_BLOCK"
        )


def module_revision() -> str:
    """Identify the ODR source revision that produced the results.

    Returns:
        The git commit of this repository, suffixed with ``-dirty`` when the
        working tree has uncommitted changes, or ``unknown`` outside git.
    """
    repo = Path(__file__).resolve().parent
    try:
        commit = subprocess.run(
            ["git", "-C", str(repo), "rev-parse", "HEAD"],
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
        dirty = subprocess.run(
            ["git", "-C", str(repo), "status", "--porcelain", "--untracked-files=no"],
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"
    return f"{commit}-dirty" if dirty else commit


def run_scenario(args: argparse.Namespace) -> Path:
    """Build, run and export one replication.

    Args:
        args: Validated scenario arguments.

    Returns:
        The path of the metadata file, which lists every other output.
    """
    args.outdir.mkdir(parents=True, exist_ok=True)
    tag = scenario_tag(args)
    prefix = args.outdir / tag

    ns.RngSeedManager.SetSeed(args.seed)
    ns.RngSeedManager.SetRun(args.run)
    apply_attribute_defaults(args)

    nodes = ns.NodeContainer()
    nodes.Create(args.nodes)
    install_mobility(nodes, args)
    devices, phy = install_wifi(nodes, args)
    install_internet(nodes, args.protocol)

    addresses = ns.Ipv4AddressHelper()
    # A /16 leaves room for scenarios beyond the 253 hosts of a /24.
    addresses.SetBase(ns.Ipv4Address("10.1.0.0"), ns.Ipv4Mask("255.255.0.0"))
    interfaces = addresses.Assign(devices)
    flows = install_traffic(nodes, interfaces, args)

    control_monitor = ns.CreateObject[ns.odr.ControlTrafficMonitor]()
    control_monitor.AddPort(CONTROL_PORTS[args.protocol])
    control_monitor.Install(nodes)

    outputs = {
        "flowmon": f"{tag}.flowmon.xml",
        "control": f"{tag}.control.csv",
        "meta": f"{tag}.meta.json",
    }
    if args.mobility_trace:
        ascii_helper = ns.AsciiTraceHelper()
        outputs["mobility"] = f"{tag}.mobility.txt"
        ns.MobilityHelper.EnableAsciiAll(
            ascii_helper.CreateFileStream(str(args.outdir / outputs["mobility"]))
        )
    if args.pcap:
        phy.EnablePcapAll(str(prefix))

    flowmon_helper = ns.FlowMonitorHelper()
    monitor = flowmon_helper.InstallAll()

    wall_start = time.monotonic()
    ns.Simulator.Stop(ns.Seconds(args.sim_time))
    ns.Simulator.Run()
    wall_seconds = time.monotonic() - wall_start

    # Flows still waiting for their last packets are resolved here, with the
    # FlowMonitor's MaxPerHopDelay rule; the analysis must still compute loss
    # as tx - rx, since packets parked by a routing protocol are in neither
    # the received nor the lost count.
    monitor.CheckForLostPackets()
    monitor.SerializeToXmlFile(str(args.outdir / outputs["flowmon"]), False, False)
    control_monitor.WriteCsv(str(args.outdir / outputs["control"]))
    for spec, source in flows:
        spec.attempted = int(source.GetAttempted())
        spec.accepted = int(source.GetAccepted())
        spec.refused = int(source.GetRefused())
    if args.protocol == "odr":
        outputs["odr"] = f"{tag}.odr.csv"
        ns.OdrHelper.WriteStatistics(nodes, str(args.outdir / outputs["odr"]))
    ns.Simulator.Destroy()

    metadata = {
        "tag": tag,
        "config_id": config_id(args),
        "protocol_variant": protocol_variant(args),
        "parameters": {
            key: (str(value) if isinstance(value, Path) else value)
            for key, value in vars(args).items()
        },
        "control_port": CONTROL_PORTS[args.protocol],
        "flows": [asdict(spec) for spec, _ in flows],
        "outputs": outputs,
        "module_revision": module_revision(),
        "wall_clock_s": round(wall_seconds, 3),
    }
    meta_path = args.outdir / outputs["meta"]
    meta_path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return meta_path


def main(argv: Sequence[str]) -> int:
    """Entry point.

    Args:
        argv: Command-line arguments, program name excluded.

    Returns:
        The process exit status.
    """
    args = parse_args(argv)
    existing = args.outdir / f"{scenario_tag(args)}.meta.json"
    if args.skip_existing and existing.exists():
        print(existing)
        return 0
    meta_path = run_scenario(args)
    print(meta_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
