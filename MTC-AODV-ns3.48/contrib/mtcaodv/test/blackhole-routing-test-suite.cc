/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Integration tests for the Gate 1B AODV-interoperable Blackhole protocol.
 *
 * The scenarios use a deterministic range-based propagation model and fixed
 * node positions, so connectivity is a property of the geometry rather than of
 * a fading realization.  This keeps the assertions about delivery exact.
 */

#include "ns3/attack-manager.h"
#include "ns3/blackhole-aodv-helper.h"
#include "ns3/blackhole-aodv-routing-protocol.h"
#include "ns3/blackhole-behavior.h"

#include "ns3/aodv-helper.h"
#include "ns3/boolean.h"
#include "ns3/inet-socket-address.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-interface-container.h"
#include "ns3/mobility-helper.h"
#include "ns3/on-off-helper.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/packet-sink.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/simulator.h"
#include "ns3/ssid.h"
#include "ns3/string.h"
#include "ns3/test.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-helper.h"
#include "ns3/yans-wifi-helper.h"

#include <cstdint>
#include <set>
#include <vector>

namespace ns3
{
namespace mtcaodv
{

/**
 * @brief Radio range used by every scenario in this suite, in metres.
 *
 * A hard range makes the intended topology exact: two nodes are neighbours if
 * and only if their fixed positions are within this distance.
 */
constexpr double SCENARIO_RANGE_METRES = 80.0;

/**
 * @brief Outcome of one scenario execution.
 */
struct ScenarioOutcome
{
    /** @brief Application bytes received by the sink. */
    uint64_t receivedBytes{0};

    /** @brief Number of forged route replies emitted by attackers. */
    uint32_t forgedReplyCount{0};

    /** @brief Number of transit packets discarded by attackers. */
    uint32_t attackDropCount{0};
};

/**
 * @brief Counts forged replies produced anywhere in the scenario.
 * @param counter Accumulator owned by the caller.
 */
static void
CountForgedReply(uint32_t* counter, Ipv4Address, Ipv4Address, uint32_t)
{
    ++(*counter);
}

/**
 * @brief Counts transit drops produced anywhere in the scenario.
 * @param counter Accumulator owned by the caller.
 */
static void
CountAttackDrop(uint32_t* counter, Ptr<const Packet>, const Ipv4Header&)
{
    ++(*counter);
}

/**
 * @brief Build, run and measure one fixed-geometry MANET scenario.
 *
 * @param positions Fixed node positions; the first node is the traffic source
 *        and the second is the traffic destination.
 * @param attackerNodeIds Nodes to install the malicious protocol on. Every
 *        other node runs the unmodified stock AODV protocol.
 * @param useForkForHonestNodes When true, honest nodes run the forked protocol
 *        with no attack policy instead of stock AODV. This is the regression
 *        bridge: the fork must then behave exactly like the baseline.
 * @return Delivered bytes and attack-event counts.
 */
static ScenarioOutcome
RunScenario(const std::vector<Vector>& positions,
            const std::set<uint32_t>& attackerNodeIds,
            bool useForkForHonestNodes)
{
    RngSeedManager::SetSeed(20260903);
    RngSeedManager::SetRun(1);

    NodeContainer nodes;
    nodes.Create(positions.size());

    Ptr<ListPositionAllocator> positionAllocator = CreateObject<ListPositionAllocator>();

    // Fixed positions make the neighbour relation deterministic, which is what
    // lets the assertions below be exact rather than statistical.
    for (const Vector& position : positions)
    {
        positionAllocator->Add(position);
    }

    MobilityHelper mobility;
    mobility.SetPositionAllocator(positionAllocator);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::RangePropagationLossModel",
                               "MaxRange",
                               DoubleValue(SCENARIO_RANGE_METRES));

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

    // Split the population exactly as a campaign scenario would, then install
    // a different routing helper on each side.  Honest and malicious nodes
    // therefore run different protocol implementations that share the stock
    // AODV wire format.
    NodeContainer honestNodes;
    NodeContainer attackerNodes;
    for (uint32_t index = 0; index < nodes.GetN(); ++index)
    {
        if (attackerNodeIds.find(nodes.Get(index)->GetId()) != attackerNodeIds.end())
        {
            attackerNodes.Add(nodes.Get(index));
        }
        else
        {
            honestNodes.Add(nodes.Get(index));
        }
    }

    AodvHelper stockAodv;
    BlackholeAodvHelper benignFork;
    BlackholeAodvHelper attackFork;
    attackFork.SetAttackBehaviorAttribute("AttackStartTime", TimeValue(Seconds(1)));

    InternetStackHelper internet;
    if (honestNodes.GetN() > 0)
    {
        if (useForkForHonestNodes)
        {
            // The benign fork keeps its default policy but never activates it,
            // because a policy that has not started is observationally AODV.
            benignFork.SetAttackBehaviorAttribute("AttackStartTime", TimeValue(Seconds(1e6)));
            internet.SetRoutingHelper(benignFork);
        }
        else
        {
            internet.SetRoutingHelper(stockAodv);
        }
        internet.Install(honestNodes);
    }
    if (attackerNodes.GetN() > 0)
    {
        internet.SetRoutingHelper(attackFork);
        internet.Install(attackerNodes);
    }

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    const uint16_t port = 9;
    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApplications = sinkHelper.Install(nodes.Get(1));
    sinkApplications.Start(Seconds(0));
    sinkApplications.Stop(Seconds(20));

    OnOffHelper sourceHelper("ns3::UdpSocketFactory",
                             InetSocketAddress(interfaces.GetAddress(1), port));
    sourceHelper.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    sourceHelper.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    sourceHelper.SetAttribute("DataRate", StringValue("8kbps"));
    sourceHelper.SetAttribute("PacketSize", UintegerValue(64));
    ApplicationContainer sourceApplications = sourceHelper.Install(nodes.Get(0));
    sourceApplications.Start(Seconds(4));
    sourceApplications.Stop(Seconds(18));

    ScenarioOutcome outcome;

    // Traces are connected on every malicious node so the counts describe the
    // whole scenario rather than one assumed attacker.
    for (uint32_t index = 0; index < attackerNodes.GetN(); ++index)
    {
        Ptr<Ipv4> ipv4 = attackerNodes.Get(index)->GetObject<Ipv4>();
        Ptr<BlackholeAodvRoutingProtocol> protocol =
            DynamicCast<BlackholeAodvRoutingProtocol>(ipv4->GetRoutingProtocol());
        NS_ASSERT_MSG(protocol, "attacker node is not running the malicious protocol");
        protocol->TraceConnectWithoutContext(
            "ForgedReply",
            MakeBoundCallback(&CountForgedReply, &outcome.forgedReplyCount));
        protocol->TraceConnectWithoutContext(
            "AttackDrop",
            MakeBoundCallback(&CountAttackDrop, &outcome.attackDropCount));
    }

    Simulator::Stop(Seconds(20));
    Simulator::Run();

    Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApplications.Get(0));
    outcome.receivedBytes = sink->GetTotalRx();

    Simulator::Destroy();
    return outcome;
}

/**
 * @brief The fork without an active policy must behave like stock AODV.
 */
class ForkRegressionTestCase : public TestCase
{
  public:
    /** @brief Construct the regression-bridge test. */
    ForkRegressionTestCase()
        : TestCase("MTC-AODV fork without an active attack policy delivers like stock AODV")
    {
    }

  private:
    /** @brief Compare stock AODV and the inert fork on one fixed chain. */
    void DoRun() override
    {
        // A three-hop chain: only consecutive nodes are within range, so every
        // delivered byte had to traverse the two intermediate relays.
        const std::vector<Vector> chain = {
            Vector(0, 0, 0),    // source
            Vector(210, 0, 0),  // destination
            Vector(70, 0, 0),   // relay
            Vector(140, 0, 0),  // relay
        };

        const ScenarioOutcome baseline = RunScenario(chain, {}, false);
        const ScenarioOutcome fork = RunScenario(chain, {}, true);

        NS_TEST_EXPECT_MSG_GT(baseline.receivedBytes,
                              0,
                              "The stock AODV baseline delivered nothing; the scenario is invalid");
        NS_TEST_EXPECT_MSG_EQ(fork.receivedBytes,
                              baseline.receivedBytes,
                              "The inert fork did not reproduce stock AODV delivery exactly");
        NS_TEST_EXPECT_MSG_EQ(fork.forgedReplyCount, 0, "An inert fork forged a route reply");
        NS_TEST_EXPECT_MSG_EQ(fork.attackDropCount, 0, "An inert fork dropped transit data");
    }
};

/**
 * @brief An on-path Blackhole must discard the transit data it attracted.
 */
class OnPathBlackholeTestCase : public TestCase
{
  public:
    /** @brief Construct the transit-drop integration test. */
    OnPathBlackholeTestCase()
        : TestCase("MTC-AODV Blackhole on the only path discards all transit data")
    {
    }

  private:
    /** @brief Compare delivery with and without a malicious relay. */
    void DoRun() override
    {
        // A two-hop chain whose single relay is the node under test.
        const std::vector<Vector> chain = {
            Vector(0, 0, 0),   // source
            Vector(140, 0, 0), // destination
            Vector(70, 0, 0),  // relay, node id 2
        };

        const ScenarioOutcome benign = RunScenario(chain, {}, false);
        const ScenarioOutcome attacked = RunScenario(chain, {2}, false);

        NS_TEST_EXPECT_MSG_GT(benign.receivedBytes,
                              0,
                              "The benign chain delivered nothing; the scenario is invalid");
        NS_TEST_EXPECT_MSG_EQ(attacked.receivedBytes,
                              0,
                              "A full Blackhole on the only path still delivered data");
        NS_TEST_EXPECT_MSG_GT(attacked.attackDropCount,
                              0,
                              "The attack drop trace never fired despite lost traffic");
    }
};

/**
 * @brief An off-path Blackhole must capture the route with a forged reply.
 */
class ForgedReplyCaptureTestCase : public TestCase
{
  public:
    /** @brief Construct the forged-RREP integration test. */
    ForgedReplyCaptureTestCase()
        : TestCase("MTC-AODV Blackhole captures a route it does not have with a forged reply")
    {
    }

  private:
    /** @brief Verify that an off-path attacker diverts and then drops traffic. */
    void DoRun() override
    {
        // The attacker at node id 3 hears the source but not the destination,
        // so it has no route to advertise.  Only a forged reply can divert the
        // traffic through it, which separates hook 1 from hook 2.
        const std::vector<Vector> topology = {
            Vector(0, 0, 0),   // source
            Vector(140, 0, 0), // destination
            Vector(70, 0, 0),  // legitimate relay, node id 2
            Vector(0, 60, 0),  // attacker, node id 3: in range of the source only
        };

        const ScenarioOutcome benign = RunScenario(topology, {}, false);
        const ScenarioOutcome attacked = RunScenario(topology, {3}, false);

        NS_TEST_EXPECT_MSG_GT(benign.receivedBytes,
                              0,
                              "The benign topology delivered nothing; the scenario is invalid");
        NS_TEST_EXPECT_MSG_GT(attacked.forgedReplyCount,
                              0,
                              "The off-path attacker never emitted a forged route reply");
        NS_TEST_EXPECT_MSG_LT(attacked.receivedBytes,
                              benign.receivedBytes,
                              "The forged reply did not reduce delivery below the benign case");
    }
};

/**
 * @brief `PartitionByAttackers` must reproduce a selection exactly.
 */
class PartitionByAttackersTestCase : public TestCase
{
  public:
    /** @brief Construct the population-split test. */
    PartitionByAttackersTestCase()
        : TestCase("MTC-AODV splits a population exactly as its selection record declares")
    {
    }

  private:
    /** @brief Verify sizes, membership and rejection of a foreign record. */
    void DoRun() override
    {
        RngSeedManager::SetSeed(12345);
        RngSeedManager::SetRun(1);

        NodeContainer nodes;
        nodes.Create(20);

        AttackManager manager;
        manager.AssignStream(73001);
        const AttackSelectionResult selection = manager.SelectAttackers(nodes, 0.20, {});

        NodeContainer honestNodes;
        NodeContainer attackerNodes;
        AttackManager::PartitionByAttackers(nodes, selection, honestNodes, attackerNodes);

        NS_TEST_EXPECT_MSG_EQ(attackerNodes.GetN(),
                              selection.attackerCount,
                              "The malicious container size differs from the declared count");
        NS_TEST_EXPECT_MSG_EQ(honestNodes.GetN() + attackerNodes.GetN(),
                              nodes.GetN(),
                              "The split lost or duplicated nodes");

        const std::set<uint32_t> declaredAttackers(selection.attackerNodeIds.begin(),
                                                    selection.attackerNodeIds.end());

        // Each container must contain exactly the identities the record names.
        for (uint32_t index = 0; index < attackerNodes.GetN(); ++index)
        {
            const bool isDeclared =
                declaredAttackers.count(attackerNodes.Get(index)->GetId()) == 1;
            NS_TEST_EXPECT_MSG_EQ(isDeclared, true, "An undeclared node was made malicious");
        }
        for (uint32_t index = 0; index < honestNodes.GetN(); ++index)
        {
            const bool isDeclared = declaredAttackers.count(honestNodes.Get(index)->GetId()) == 1;
            NS_TEST_EXPECT_MSG_EQ(isDeclared, false, "A declared attacker was left honest");
        }

        // A record describing a different population must be refused rather
        // than silently producing a smaller attacker set.
        AttackSelectionResult foreignSelection = selection;
        foreignSelection.nodeCount = nodes.GetN() + 1;
        bool exceptionObserved = false;
        try
        {
            NodeContainer discardedHonest;
            NodeContainer discardedAttackers;
            AttackManager::PartitionByAttackers(nodes,
                                                foreignSelection,
                                                discardedHonest,
                                                discardedAttackers);
        }
        catch (const std::exception&)
        {
            exceptionObserved = true;
        }
        NS_TEST_EXPECT_MSG_EQ(exceptionObserved,
                              true,
                              "A selection for another population was accepted");

        Simulator::Destroy();
    }
};

/**
 * @brief Registers the Gate 1B routing tests with ns-3.
 */
class BlackholeRoutingTestSuite : public TestSuite
{
  public:
    /** @brief Construct and register all Gate 1B routing test cases. */
    BlackholeRoutingTestSuite()
        : TestSuite("mtcaodv-blackhole-routing", Type::UNIT)
    {
        AddTestCase(new PartitionByAttackersTestCase, TestCase::Duration::QUICK);
        AddTestCase(new ForkRegressionTestCase, TestCase::Duration::QUICK);
        AddTestCase(new OnPathBlackholeTestCase, TestCase::Duration::QUICK);
        AddTestCase(new ForgedReplyCaptureTestCase, TestCase::Duration::QUICK);
    }
};

/** Static registration object discovered automatically by the ns-3 test runner. */
static BlackholeRoutingTestSuite g_blackholeRoutingTestSuite;

} // namespace mtcaodv
} // namespace ns3
