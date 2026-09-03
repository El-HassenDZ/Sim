/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Gate 1B executable: measures the effect of simultaneous Blackholes on a
 * 100-node MANET, using the stock AODV protocol for honest nodes and the
 * AODV-interoperable malicious protocol for the deterministically selected
 * attackers.
 *
 * The scenario is a measurement instrument, not a defence: no MTC-AODV
 * detector, trust model or ledger participates.  Its purpose is to establish
 * that the attack is observable, reproducible, and identical across paired
 * runs before any defensive component is written.
 */

#include "ns3/attack-manager.h"
#include "ns3/blackhole-aodv-helper.h"
#include "ns3/blackhole-aodv-routing-protocol.h"

#include "ns3/aodv-helper.h"
#include "ns3/command-line.h"
#include "ns3/double.h"
#include "ns3/inet-socket-address.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/on-off-helper.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/packet-sink.h"
#include "ns3/pointer.h"
#include "ns3/position-allocator.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-helper.h"
#include "ns3/yans-wifi-helper.h"

#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <set>
#include <vector>

using namespace ns3;

/** @brief Application packets handed to the transport layer by all sources. */
static uint64_t g_transmittedPackets = 0;

/** @brief Application packets delivered to all sinks. */
static uint64_t g_receivedPackets = 0;

/** @brief Forged route replies emitted by all attackers. */
static uint64_t g_forgedReplies = 0;

/** @brief Transit packets discarded by all attackers. */
static uint64_t g_attackDrops = 0;

/**
 * @brief Count one packet handed to the transport layer by a source.
 * @param packet The transmitted packet, unused beyond counting.
 */
static void
CountTransmitted(Ptr<const Packet> packet)
{
    ++g_transmittedPackets;
}

/**
 * @brief Count one packet delivered to a sink.
 * @param packet The received packet, unused beyond counting.
 * @param address Source address, unused beyond counting.
 */
static void
CountReceived(Ptr<const Packet> packet, const Address& address)
{
    ++g_receivedPackets;
}

/**
 * @brief Count one forged route reply.
 */
static void
CountForgedReply(Ipv4Address, Ipv4Address, uint32_t)
{
    ++g_forgedReplies;
}

/**
 * @brief Count one transit packet discarded by an attacker.
 */
static void
CountAttackDrop(Ptr<const Packet>, const Ipv4Header&)
{
    ++g_attackDrops;
}

/**
 * @brief Run one Gate 1B scenario and print a one-line JSON manifest.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return zero on success; two for a rejected configuration.
 */
int
main(int argc, char* argv[])
{
    /** Total MANET population. Unit: nodes. */
    uint32_t nodeCount = 100;

    /** Fraction of malicious nodes. Domain: [0,1]. */
    double attackerRatio = 0.0;

    /** Global seed shared by paired variants. */
    uint32_t seed = 12345;

    /** Replication index. */
    uint64_t run = 1;

    /** Stream reserved for attacker identities only. */
    int64_t attackerSelectionStream = 73001;

    /** Number of concurrent constant-bit-rate flows. */
    uint32_t flowCount = 10;

    /** Simulated duration, in seconds. */
    double durationSeconds = 60.0;

    /** Simulation time at which malicious behaviour begins, in seconds. */
    double attackStartSeconds = 10.0;

    /** Deployment area width and height, in metres. */
    double areaWidth = 1500.0;
    double areaHeight = 300.0;

    /** Maximum node speed for the random-waypoint model, in metres per second. */
    double maximumSpeed = 5.0;

    /**
     * @brief Offered rate of each constant-bit-rate flow, in kilobits per second.
     *
     * The total offered load is the dominant control on the no-attack baseline
     * in a shared 802.11 collision domain, so it is an explicit experimental
     * parameter rather than a constant buried in the scenario.
     */
    double flowRateKbps = 4.0;

    /** Application payload size of every flow, in bytes. */
    uint32_t packetSizeBytes = 64;

    CommandLine commandLine(__FILE__);
    commandLine.AddValue("nodeCount", "Total number of MANET nodes.", nodeCount);
    commandLine.AddValue("attackerRatio", "Attacker proportion in [0,1].", attackerRatio);
    commandLine.AddValue("seed", "Global ns-3 seed.", seed);
    commandLine.AddValue("run", "ns-3 replication run number.", run);
    commandLine.AddValue("attackStream", "RNG stream for attacker selection.",
                         attackerSelectionStream);
    commandLine.AddValue("flowCount", "Number of concurrent CBR flows.", flowCount);
    commandLine.AddValue("duration", "Simulated duration in seconds.", durationSeconds);
    commandLine.AddValue("attackStart", "Attack activation time in seconds.", attackStartSeconds);
    commandLine.AddValue("areaWidth", "Deployment area width in metres.", areaWidth);
    commandLine.AddValue("areaHeight", "Deployment area height in metres.", areaHeight);
    commandLine.AddValue("maxSpeed", "Maximum node speed in metres per second.", maximumSpeed);
    commandLine.AddValue("flowRateKbps", "Offered rate of each flow in kbit/s.", flowRateKbps);
    commandLine.AddValue("packetSize", "Application payload size in bytes.", packetSizeBytes);
    commandLine.Parse(argc, argv);

    try
    {
        // Seed and run are fixed before any random variable exists, which is
        // what makes a paired rerun bit-identical.
        RngSeedManager::SetSeed(seed);
        RngSeedManager::SetRun(run);

        if (flowCount == 0 || 2 * flowCount > nodeCount)
        {
            throw std::invalid_argument("flowCount must be positive and leave distinct endpoints");
        }

        NodeContainer nodes;
        nodes.Create(nodeCount);

        // Traffic endpoints are protected from malicious assignment so that the
        // attacker ratio changes the relay population only, never the offered
        // load. Sources are the first flowCount nodes, sinks the last.
        std::set<uint32_t> excludedNodeIds;
        for (uint32_t index = 0; index < flowCount; ++index)
        {
            excludedNodeIds.insert(nodes.Get(index)->GetId());
            excludedNodeIds.insert(nodes.Get(nodeCount - 1 - index)->GetId());
        }

        mtcaodv::AttackManager attackManager;
        attackManager.AssignStream(attackerSelectionStream);
        const mtcaodv::AttackSelectionResult selection =
            attackManager.SelectAttackers(nodes, attackerRatio, excludedNodeIds);

        NodeContainer honestNodes;
        NodeContainer attackerNodes;
        mtcaodv::AttackManager::PartitionByAttackers(nodes, selection, honestNodes, attackerNodes);

        MobilityHelper mobility;
        mobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator",
                                      "X",
                                      StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" +
                                                  std::to_string(areaWidth) + "]"),
                                      "Y",
                                      StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" +
                                                  std::to_string(areaHeight) + "]"));
        Ptr<PositionAllocator> waypointAllocator =
            CreateObjectWithAttributes<RandomRectanglePositionAllocator>(
                "X",
                StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" +
                            std::to_string(areaWidth) + "]"),
                "Y",
                StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" +
                            std::to_string(areaHeight) + "]"));
        mobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                                  "Speed",
                                  StringValue("ns3::UniformRandomVariable[Min=1.0|Max=" +
                                              std::to_string(maximumSpeed) + "]"),
                                  "Pause",
                                  StringValue("ns3::ConstantRandomVariable[Constant=0.0]"),
                                  "PositionAllocator",
                                  PointerValue(waypointAllocator));
        mobility.Install(nodes);

        YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
        YansWifiPhyHelper phy;
        phy.SetChannel(channel.Create());

        WifiMacHelper mac;
        mac.SetType("ns3::AdhocWifiMac");

        WifiHelper wifi;
        wifi.SetStandard(WIFI_STANDARD_80211b);
        wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                     "DataMode",
                                     StringValue("DsssRate11Mbps"),
                                     "ControlMode",
                                     StringValue("DsssRate1Mbps"));
        NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

        // Honest nodes run the unmodified stock protocol.  Attackers run the
        // interoperable fork.  Both speak the same AODV wire format, so the
        // attack needs no protocol negotiation to succeed.
        AodvHelper stockAodv;
        mtcaodv::BlackholeAodvHelper blackholeAodv;
        blackholeAodv.SetAttackBehaviorAttribute("AttackStartTime",
                                                 TimeValue(Seconds(attackStartSeconds)));

        InternetStackHelper internet;
        internet.SetRoutingHelper(stockAodv);
        internet.Install(honestNodes);
        if (attackerNodes.GetN() > 0)
        {
            internet.SetRoutingHelper(blackholeAodv);
            internet.Install(attackerNodes);
        }

        Ipv4AddressHelper address;
        address.SetBase("10.1.0.0", "255.255.0.0");
        Ipv4InterfaceContainer interfaces = address.Assign(devices);

        const uint16_t basePort = 9000;
        ApplicationContainer sinkApplications;
        ApplicationContainer sourceApplications;

        // Each flow pairs one low-index source with one high-index sink, so
        // routes are long enough for intermediate nodes to matter.
        for (uint32_t index = 0; index < flowCount; ++index)
        {
            const uint32_t sourceIndex = index;
            const uint32_t sinkIndex = nodeCount - 1 - index;
            const uint16_t port = basePort + index;

            PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                        InetSocketAddress(Ipv4Address::GetAny(), port));
            sinkApplications.Add(sinkHelper.Install(nodes.Get(sinkIndex)));

            OnOffHelper sourceHelper(
                "ns3::UdpSocketFactory",
                InetSocketAddress(interfaces.GetAddress(sinkIndex), port));
            sourceHelper.SetAttribute("OnTime",
                                      StringValue("ns3::ConstantRandomVariable[Constant=1]"));
            sourceHelper.SetAttribute("OffTime",
                                      StringValue("ns3::ConstantRandomVariable[Constant=0]"));
            sourceHelper.SetAttribute(
                "DataRate",
                StringValue(std::to_string(flowRateKbps) + "kbps"));
            sourceHelper.SetAttribute("PacketSize", UintegerValue(packetSizeBytes));
            sourceApplications.Add(sourceHelper.Install(nodes.Get(sourceIndex)));
        }

        sinkApplications.Start(Seconds(0.0));
        sinkApplications.Stop(Seconds(durationSeconds));
        sourceApplications.Start(Seconds(attackStartSeconds + 5.0));
        sourceApplications.Stop(Seconds(durationSeconds - 1.0));

        // Counting at the application boundary keeps the PDR definition free of
        // any routing-layer interpretation of what a delivered packet is.
        for (uint32_t index = 0; index < sourceApplications.GetN(); ++index)
        {
            sourceApplications.Get(index)->TraceConnectWithoutContext(
                "Tx",
                MakeCallback(&CountTransmitted));
        }
        for (uint32_t index = 0; index < sinkApplications.GetN(); ++index)
        {
            sinkApplications.Get(index)->TraceConnectWithoutContext("Rx",
                                                                    MakeCallback(&CountReceived));
        }
        for (uint32_t index = 0; index < attackerNodes.GetN(); ++index)
        {
            Ptr<Ipv4> ipv4 = attackerNodes.Get(index)->GetObject<Ipv4>();
            Ptr<mtcaodv::BlackholeAodvRoutingProtocol> protocol =
                DynamicCast<mtcaodv::BlackholeAodvRoutingProtocol>(ipv4->GetRoutingProtocol());
            NS_ASSERT_MSG(protocol, "an attacker is not running the malicious protocol");
            protocol->TraceConnectWithoutContext("ForgedReply", MakeCallback(&CountForgedReply));
            protocol->TraceConnectWithoutContext("AttackDrop", MakeCallback(&CountAttackDrop));
        }

        Simulator::Stop(Seconds(durationSeconds));
        Simulator::Run();
        Simulator::Destroy();

        // A zero denominator is reported as a null ratio rather than as zero,
        // because "no packet was offered" and "no packet arrived" are different
        // experimental outcomes and must not be aggregated together.
        std::cout << std::fixed << std::setprecision(6) << "{\"schemaVersion\":1"
                  << ",\"gate\":\"1B\""
                  << ",\"nodeCount\":" << nodeCount << ",\"attackerRatio\":" << attackerRatio
                  << ",\"attackerCount\":" << selection.attackerCount << ",\"seed\":" << seed
                  << ",\"run\":" << run
                  << ",\"attackerSelectionStream\":" << attackerSelectionStream
                  << ",\"flowCount\":" << flowCount << ",\"flowRateKbps\":" << flowRateKbps
                  << ",\"packetSizeBytes\":" << packetSizeBytes
                  << ",\"areaWidth\":" << areaWidth << ",\"areaHeight\":" << areaHeight
                  << ",\"maxSpeed\":" << maximumSpeed << ",\"transmittedPackets\":"
                  << g_transmittedPackets << ",\"receivedPackets\":" << g_receivedPackets
                  << ",\"packetDeliveryRatio\":";
        if (g_transmittedPackets == 0)
        {
            std::cout << "null";
        }
        else
        {
            std::cout << (static_cast<double>(g_receivedPackets) /
                          static_cast<double>(g_transmittedPackets));
        }
        std::cout << ",\"forgedReplies\":" << g_forgedReplies
                  << ",\"attackDrops\":" << g_attackDrops << ",\"attackerNodeIds\":[";
        bool isFirst = true;
        for (const uint32_t nodeId : selection.attackerNodeIds)
        {
            if (!isFirst)
            {
                std::cout << ',';
            }
            std::cout << nodeId;
            isFirst = false;
        }
        std::cout << "]}" << std::endl;
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "MTC-AODV Gate 1B configuration error: " << exception.what() << std::endl;
        Simulator::Destroy();
        return 2;
    }
}
