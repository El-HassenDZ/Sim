#include "ns3/double.h"
#include "ns3/inet-socket-address.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/odr-helper.h"
#include "ns3/odr-packet-queue.h"
#include "ns3/odr-packet.h"
#include "ns3/odr-request-cache.h"
#include "ns3/odr-routing-table.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/test.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/wifi-helper.h"
#include "ns3/wifi-mac-helper.h"
#include "ns3/yans-wifi-helper.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <vector>

using namespace ns3;
using namespace ns3::odr;

namespace
{

/// Radio range of the test topologies. RangePropagationLossModel makes links
/// binary, so each scenario controls exactly which nodes are neighbors.
constexpr double RADIO_RANGE_M = 150.0;
constexpr uint16_t SINK_PORT = 9;

/**
 * Build an 802.11b ad hoc network running ODR, one node per position.
 *
 * @param positions initial node positions
 * @param nodes receives the created nodes
 * @return the assigned IPv4 interfaces, in node order
 */
Ipv4InterfaceContainer
BuildNetwork(const std::vector<Vector>& positions, NodeContainer& nodes)
{
    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(1);
    nodes.Create(positions.size());

    Ptr<ListPositionAllocator> allocator = CreateObject<ListPositionAllocator>();
    for (const Vector& position : positions)
    {
        allocator->Add(position);
    }
    MobilityHelper mobility;
    mobility.SetPositionAllocator(allocator);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::RangePropagationLossModel",
                               "MaxRange",
                               DoubleValue(RADIO_RANGE_M));
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

    OdrHelper odr;
    InternetStackHelper stack;
    stack.SetRoutingHelper(odr);
    stack.Install(nodes);
    odr.AssignStreams(nodes, 0);
    wifi.AssignStreams(devices, 100);

    Ipv4AddressHelper address;
    address.SetBase("10.0.0.0", "255.255.255.0");
    return address.Assign(devices);
}

/**
 * Send count UDP datagrams of 64 bytes, one every interval.
 *
 * @param src sending node
 * @param dst destination address
 * @param start first transmission time
 * @param interval spacing between transmissions
 * @param count number of datagrams
 */
void
StartUdpFlow(Ptr<Node> src, Ipv4Address dst, Time start, Time interval, uint32_t count)
{
    Ptr<Socket> socket = Socket::CreateSocket(src, UdpSocketFactory::GetTypeId());
    socket->Bind();
    for (uint32_t k = 0; k < count; ++k)
    {
        // Times are computed from the index, not accumulated, so that two flows
        // meant to start together are not skewed by rounding.
        Simulator::ScheduleWithContext(src->GetId(), start + interval * k, [socket, dst]() {
            socket->SendTo(Create<Packet>(64), 0, InetSocketAddress(dst, SINK_PORT));
        });
    }
}

/**
 * Datagram counter bound to SINK_PORT on one node.
 */
class UdpSink
{
  public:
    /**
     * @param node node to listen on
     */
    explicit UdpSink(Ptr<Node> node)
        : m_socket(Socket::CreateSocket(node, UdpSocketFactory::GetTypeId()))
    {
        m_socket->Bind(InetSocketAddress(Ipv4Address::GetAny(), SINK_PORT));
        m_socket->SetRecvCallback(MakeCallback(&UdpSink::Receive, this));
    }

    /**
     * @param from source address
     * @return datagrams received from that source
     */
    uint32_t GetReceived(Ipv4Address from) const
    {
        auto it = m_perSource.find(from);
        return it == m_perSource.end() ? 0 : it->second;
    }

    /**
     * @param after lower time bound
     * @return datagrams received strictly after that time, all sources
     */
    uint32_t GetReceivedAfter(Time after) const
    {
        return static_cast<uint32_t>(std::count_if(m_arrivals.begin(),
                                                   m_arrivals.end(),
                                                   [after](Time t) { return t > after; }));
    }

  private:
    /**
     * @param socket the listening socket
     */
    void Receive(Ptr<Socket> socket)
    {
        Address from;
        while (socket->RecvFrom(from))
        {
            ++m_perSource[InetSocketAddress::ConvertFrom(from).GetIpv4()];
            m_arrivals.push_back(Simulator::Now());
        }
    }

    Ptr<Socket> m_socket;                        //!< Listening socket
    std::map<Ipv4Address, uint32_t> m_perSource; //!< Datagrams per source
    std::vector<Time> m_arrivals;                //!< Arrival times
};

/**
 * Move a node instantly, to create or break links at a chosen time.
 *
 * @param node the node
 * @param at time of the move
 * @param position new position
 */
void
ScheduleMove(Ptr<Node> node, Time at, Vector position)
{
    Simulator::Schedule(at, [node, position]() {
        node->GetObject<MobilityModel>()->SetPosition(position);
    });
}

/**
 * @param node a node with ODR
 * @return its ODR statistics
 */
const Statistics&
StatsOf(Ptr<Node> node)
{
    return OdrHelper::GetRoutingProtocol(node)->GetStatistics();
}

} // namespace

/**
 * @ingroup odr-tests
 * Serialization of every ODR message, including on-air sizes, which must
 * match RFC 3561 for byte overheads to be comparable with AODV.
 */
class OdrHeaderTestCase : public TestCase
{
  public:
    OdrHeaderTestCase()
        : TestCase("ODR headers serialize to RFC 3561 sizes and round-trip")
    {
    }

  private:
    void DoRun() override
    {
        RreqHeader rreq;
        rreq.SetHopCount(7);
        rreq.SetRequestId(0xDEADBEEF);
        rreq.SetDestination(Ipv4Address("10.0.0.9"));
        rreq.SetDestinationSeqNo(42);
        rreq.SetOrigin(Ipv4Address("10.0.0.1"));
        rreq.SetOriginSeqNo(0xFFFFFFFF);
        rreq.SetDestinationOnly(true);
        rreq.SetUnknownSeqNo(true);
        Ptr<Packet> packet = Create<Packet>();
        packet->AddHeader(rreq);
        packet->AddHeader(TypeHeader(ODR_RREQ));
        NS_TEST_EXPECT_MSG_EQ(packet->GetSize(), 24U, "RREQ must be 24 bytes on air");

        TypeHeader type;
        packet->RemoveHeader(type);
        NS_TEST_EXPECT_MSG_EQ(type.IsValid(), true, "Known type rejected");
        NS_TEST_EXPECT_MSG_EQ(type.GetType(), ODR_RREQ, "Wrong type");
        RreqHeader rreqOut;
        packet->RemoveHeader(rreqOut);
        NS_TEST_EXPECT_MSG_EQ(+rreqOut.GetHopCount(), 7, "Hop count");
        NS_TEST_EXPECT_MSG_EQ(rreqOut.GetRequestId(), 0xDEADBEEF, "Request id");
        NS_TEST_EXPECT_MSG_EQ(rreqOut.GetDestination(), Ipv4Address("10.0.0.9"), "Destination");
        NS_TEST_EXPECT_MSG_EQ(rreqOut.GetDestinationSeqNo(), 42U, "Destination seqno");
        NS_TEST_EXPECT_MSG_EQ(rreqOut.GetOrigin(), Ipv4Address("10.0.0.1"), "Origin");
        NS_TEST_EXPECT_MSG_EQ(rreqOut.GetOriginSeqNo(), 0xFFFFFFFF, "Origin seqno");
        NS_TEST_EXPECT_MSG_EQ(rreqOut.IsDestinationOnly(), true, "D flag");
        NS_TEST_EXPECT_MSG_EQ(rreqOut.IsUnknownSeqNo(), true, "U flag");
        rreqOut.SetUnknownSeqNo(false);
        NS_TEST_EXPECT_MSG_EQ(rreqOut.IsDestinationOnly(), true, "Clearing U must keep D");

        RrepHeader rrep;
        rrep.SetHopCount(3);
        rrep.SetDestination(Ipv4Address("10.0.0.9"));
        rrep.SetDestinationSeqNo(43);
        rrep.SetOrigin(Ipv4Address("10.0.0.1"));
        rrep.SetLifetime(MilliSeconds(1500));
        packet = Create<Packet>();
        packet->AddHeader(rrep);
        packet->AddHeader(TypeHeader(ODR_RREP));
        NS_TEST_EXPECT_MSG_EQ(packet->GetSize(), 20U, "RREP must be 20 bytes on air");
        packet->RemoveHeader(type);
        RrepHeader rrepOut;
        packet->RemoveHeader(rrepOut);
        NS_TEST_EXPECT_MSG_EQ(+rrepOut.GetHopCount(), 3, "Hop count");
        NS_TEST_EXPECT_MSG_EQ(rrepOut.GetDestinationSeqNo(), 43U, "Destination seqno");
        NS_TEST_EXPECT_MSG_EQ(rrepOut.GetLifetime(), MilliSeconds(1500), "Lifetime");
        rrep.SetLifetime(MilliSeconds(-5));
        NS_TEST_EXPECT_MSG_EQ(rrep.GetLifetime(), Time(0), "Negative lifetime must clamp to 0");

        RerrHeader rerr;
        rerr.AddUnreachable(Ipv4Address("10.0.0.3"), 5);
        rerr.AddUnreachable(Ipv4Address("10.0.0.4"), 6);
        rerr.AddUnreachable(Ipv4Address("10.0.0.5"), 7);
        packet = Create<Packet>();
        packet->AddHeader(rerr);
        packet->AddHeader(TypeHeader(ODR_RERR));
        NS_TEST_EXPECT_MSG_EQ(packet->GetSize(), 28U, "RERR must be 4 + 8n bytes on air");
        packet->RemoveHeader(type);
        RerrHeader rerrOut;
        packet->RemoveHeader(rerrOut);
        NS_TEST_ASSERT_MSG_EQ(+rerrOut.GetDestinationCount(), 3, "Destination count");
        NS_TEST_EXPECT_MSG_EQ(rerrOut.GetUnreachable()[1].address,
                              Ipv4Address("10.0.0.4"),
                              "Order must be preserved");
        NS_TEST_EXPECT_MSG_EQ(rerrOut.GetUnreachable()[2].seqNo, 7U, "Sequence number");

        RerrHeader full;
        for (uint32_t k = 0; k < RerrHeader::MAX_DESTINATIONS; ++k)
        {
            full.AddUnreachable(Ipv4Address(k), k);
        }
        NS_TEST_EXPECT_MSG_EQ(full.AddUnreachable(Ipv4Address("10.0.0.1"), 1),
                              false,
                              "The 8-bit count must cap a RERR at 255 destinations");

        uint8_t garbage = 0x7F;
        packet = Create<Packet>(&garbage, 1);
        packet->RemoveHeader(type);
        NS_TEST_EXPECT_MSG_EQ(type.IsValid(), false, "Unknown type byte accepted");
    }
};

/**
 * @ingroup odr-tests
 * Sequence number freshness under 32-bit wrap-around.
 */
class OdrSeqNoTestCase : public TestCase
{
  public:
    OdrSeqNoTestCase()
        : TestCase("ODR sequence number comparison survives wrap-around")
    {
    }

  private:
    void DoRun() override
    {
        NS_TEST_EXPECT_MSG_EQ(IsFresher(1, 0), true, "1 after 0");
        NS_TEST_EXPECT_MSG_EQ(IsFresher(0, 1), false, "0 before 1");
        NS_TEST_EXPECT_MSG_EQ(IsFresher(5, 5), false, "Equal is not fresher");
        NS_TEST_EXPECT_MSG_EQ(IsFresher(0, 0xFFFFFFFF), true, "Rollover to 0 is fresher");
        NS_TEST_EXPECT_MSG_EQ(IsFresher(0xFFFFFFFF, 0), false, "Pre-rollover value is stale");
    }
};

/**
 * @ingroup odr-tests
 * Route replacement rule, invalidation paths and lazy expiry of the table.
 */
class OdrRoutingTableTestCase : public TestCase
{
  public:
    OdrRoutingTableTestCase()
        : TestCase("ODR routing table applies RFC 3561 update, invalidation and expiry rules")
    {
    }

  private:
    /**
     * @param dst destination
     * @param nextHop next hop
     * @param hops hop count
     * @param seqNo destination sequence number
     * @param lifetime validity from now
     * @return a candidate route
     */
    static RoutingTableEntry MakeRoute(const char* dst,
                                       const char* nextHop,
                                       uint8_t hops,
                                       uint32_t seqNo,
                                       Time lifetime)
    {
        RoutingTableEntry entry(nullptr,
                                Ipv4InterfaceAddress(),
                                Ipv4Address(dst),
                                Ipv4Address(nextHop),
                                hops,
                                Simulator::Now() + lifetime);
        entry.SetSeqNo(seqNo);
        return entry;
    }

    void CheckUpdateRules()
    {
        RoutingTable table(Seconds(10));
        const Ipv4Address dst("10.0.0.9");
        RoutingTableEntry out;

        NS_TEST_EXPECT_MSG_EQ(table.Update(MakeRoute("10.0.0.9", "10.0.0.2", 3, 10, Seconds(5))),
                              true,
                              "New destination must be accepted");
        NS_TEST_EXPECT_MSG_EQ(table.Update(MakeRoute("10.0.0.9", "10.0.0.3", 4, 10, Seconds(5))),
                              false,
                              "Same seqno and longer path must be refused");
        NS_TEST_EXPECT_MSG_EQ(table.Update(MakeRoute("10.0.0.9", "10.0.0.3", 2, 9, Seconds(5))),
                              false,
                              "Older seqno must be refused even if shorter");
        table.Find(dst)->AddPrecursor(Ipv4Address("10.0.0.7"));
        NS_TEST_EXPECT_MSG_EQ(table.Update(MakeRoute("10.0.0.9", "10.0.0.4", 2, 10, Seconds(5))),
                              true,
                              "Same seqno and shorter path must be accepted");
        NS_TEST_EXPECT_MSG_EQ(table.Update(MakeRoute("10.0.0.9", "10.0.0.5", 6, 11, Seconds(5))),
                              true,
                              "Fresher seqno must win whatever the length");
        table.Lookup(dst, out);
        NS_TEST_EXPECT_MSG_EQ(out.GetNextHop(), Ipv4Address("10.0.0.5"), "Next hop not updated");
        NS_TEST_EXPECT_MSG_EQ(out.GetPrecursors().count(Ipv4Address("10.0.0.7")),
                              1U,
                              "Precursors must survive a next-hop change");

        NS_TEST_EXPECT_MSG_EQ(table.RefreshNeighbor(nullptr,
                                                    Ipv4InterfaceAddress(),
                                                    Ipv4Address("10.0.0.5"),
                                                    Seconds(3)),
                              true,
                              "Unknown neighbor must be reported as newly reachable");
        NS_TEST_EXPECT_MSG_EQ(table.RefreshNeighbor(nullptr,
                                                    Ipv4InterfaceAddress(),
                                                    Ipv4Address("10.0.0.5"),
                                                    Seconds(3)),
                              false,
                              "Refreshing a live neighbor is not a new route");

        table.Update(MakeRoute("10.0.0.8", "10.0.0.5", 2, 20, Seconds(5)));
        table.Update(MakeRoute("10.0.0.6", "10.0.0.2", 2, 30, Seconds(5)));
        table.Find(Ipv4Address("10.0.0.8"))->AddPrecursor(Ipv4Address("10.0.0.1"));
        std::set<Ipv4Address> precursors;
        std::vector<UnreachableDestination> lost =
            table.InvalidateByNextHop(Ipv4Address("10.0.0.5"), precursors);
        NS_TEST_ASSERT_MSG_EQ(lost.size(), 3U, "Routes to 10.0.0.5, .8 and .9 all use 10.0.0.5");
        NS_TEST_EXPECT_MSG_EQ(precursors.size(), 2U, "Precursor union of the broken routes");
        table.Lookup(Ipv4Address("10.0.0.8"), out);
        NS_TEST_EXPECT_MSG_EQ(out.GetSeqNo(), 21U, "Seqno must be bumped on link break");
        NS_TEST_EXPECT_MSG_EQ(out.IsUsable(), false, "Broken route still usable");
        NS_TEST_EXPECT_MSG_EQ(table.LookupUsable(Ipv4Address("10.0.0.6"), out),
                              true,
                              "Route through another neighbor must survive");

        std::vector<UnreachableDestination> reported{{Ipv4Address("10.0.0.6"), 0},
                                                     {Ipv4Address("10.0.0.8"), 0}};
        precursors.clear();
        std::vector<UnreachableDestination> propagated =
            table.InvalidateReported(Ipv4Address("10.0.0.2"), reported, precursors);
        NS_TEST_ASSERT_MSG_EQ(propagated.size(), 1U, "Only the route via the reporter is affected");
        NS_TEST_EXPECT_MSG_EQ(propagated[0].seqNo,
                              30U,
                              "A RERR carrying 0 must not regress the known seqno");
    }

    void CheckExpiry()
    {
        // Retention after invalidation is anchored on the expiry instant: a
        // route expiring at 1 s with a 2 s delete period disappears at 3 s.
        auto table = std::make_shared<RoutingTable>(Seconds(2));
        table->Update(MakeRoute("10.0.0.9", "10.0.0.2", 1, 1, Seconds(1)));
        Simulator::Schedule(MilliSeconds(500), [this, table]() {
            RoutingTableEntry out;
            NS_TEST_EXPECT_MSG_EQ(table->LookupUsable(Ipv4Address("10.0.0.9"), out),
                                  true,
                                  "Route expired early");
        });
        Simulator::Schedule(MilliSeconds(1500), [this, table]() {
            RoutingTableEntry out;
            NS_TEST_EXPECT_MSG_EQ(table->LookupUsable(Ipv4Address("10.0.0.9"), out),
                                  false,
                                  "Expired route still usable");
            table->Purge();
            NS_TEST_EXPECT_MSG_EQ(table->GetSize(), 1U, "Invalid entry must be retained");
        });
        Simulator::Schedule(MilliSeconds(3100), [this, table]() {
            table->Purge();
            NS_TEST_EXPECT_MSG_EQ(table->GetSize(), 0U, "Invalid entry must be deleted");
        });
        Simulator::Run();
        Simulator::Destroy();
    }

    void DoRun() override
    {
        CheckUpdateRules();
        CheckExpiry();
    }
};

/**
 * @ingroup odr-tests
 * Duplicate RREQ suppression and its time-bounded memory.
 */
class OdrRequestCacheTestCase : public TestCase
{
  public:
    OdrRequestCacheTestCase()
        : TestCase("ODR request cache suppresses duplicates until expiry")
    {
    }

  private:
    void DoRun() override
    {
        auto cache = std::make_shared<RequestCache>(Seconds(2));
        const Ipv4Address origin("10.0.0.1");
        NS_TEST_EXPECT_MSG_EQ(cache->IsDuplicate(origin, 1), false, "First copy is new");
        NS_TEST_EXPECT_MSG_EQ(cache->IsDuplicate(origin, 1), true, "Second copy is a duplicate");
        NS_TEST_EXPECT_MSG_EQ(cache->IsDuplicate(origin, 2), false, "Other flood id is new");
        NS_TEST_EXPECT_MSG_EQ(cache->IsDuplicate(Ipv4Address("10.0.0.2"), 1),
                              false,
                              "Same id from another originator is new");
        Simulator::Schedule(Seconds(3), [this, cache, origin]() {
            NS_TEST_EXPECT_MSG_EQ(cache->GetSize(), 0U, "Records must expire");
            NS_TEST_EXPECT_MSG_EQ(cache->IsDuplicate(origin, 1),
                                  false,
                                  "Expired flood must be processed again");
        });
        Simulator::Run();
        Simulator::Destroy();
    }
};

/**
 * @ingroup odr-tests
 * Capacity, eviction order, duplicate refusal and expiry of the packet queue.
 */
class OdrPacketQueueTestCase : public TestCase
{
  public:
    OdrPacketQueueTestCase()
        : TestCase("ODR packet queue evicts oldest, refuses duplicates, expires")
    {
    }

  private:
    /**
     * @param queue the queue
     * @param dst packet destination
     * @return the parked entry
     */
    static QueueEntry Park(PacketQueue& queue, const char* dst)
    {
        Ipv4Header header;
        header.SetDestination(Ipv4Address(dst));
        QueueEntry entry(Create<Packet>(10),
                         header,
                         QueueEntry::UnicastForwardCallback(),
                         QueueEntry::ErrorCallback(),
                         Simulator::Now() + queue.GetMaxDelay());
        queue.Enqueue(entry);
        return entry;
    }

    void CountDrop(const QueueEntry& entry, QueueDropReason reason)
    {
        ++m_drops[reason];
    }

    void DoRun() override
    {
        auto queue = std::make_shared<PacketQueue>(3, Seconds(10));
        queue->SetDropCallback(MakeCallback(&OdrPacketQueueTestCase::CountDrop, this));

        Park(*queue, "10.0.0.1");
        Park(*queue, "10.0.0.2");
        QueueEntry third = Park(*queue, "10.0.0.1");
        QueueEntry fourth = Park(*queue, "10.0.0.1");
        NS_TEST_EXPECT_MSG_EQ(m_drops[QueueDropReason::QUEUE_FULL], 1U, "One eviction expected");
        NS_TEST_EXPECT_MSG_EQ(queue->GetSize(), 3U, "Capacity exceeded");
        NS_TEST_EXPECT_MSG_EQ(queue->Enqueue(third), false, "Duplicate packet accepted");

        std::vector<QueueEntry> released = queue->Dequeue(Ipv4Address("10.0.0.1"));
        NS_TEST_ASSERT_MSG_EQ(released.size(), 2U, "The oldest packet should have been evicted");
        NS_TEST_EXPECT_MSG_EQ(released[0].GetPacket()->GetUid(),
                              third.GetPacket()->GetUid(),
                              "Arrival order must be preserved");
        NS_TEST_EXPECT_MSG_EQ(released[1].GetPacket()->GetUid(),
                              fourth.GetPacket()->GetUid(),
                              "Arrival order must be preserved");
        NS_TEST_EXPECT_MSG_EQ(queue->Contains(Ipv4Address("10.0.0.2")), true, "Other flow lost");

        Simulator::Schedule(Seconds(11), [this, queue]() {
            NS_TEST_EXPECT_MSG_EQ(queue->GetSize(), 0U, "Expired packet still parked");
            NS_TEST_EXPECT_MSG_EQ(m_drops[QueueDropReason::EXPIRED], 1U, "Expiry not reported");
        });
        Simulator::Run();
        Simulator::Destroy();
    }

    std::map<QueueDropReason, uint32_t> m_drops; //!< Drops observed, by reason
};

/**
 * @ingroup odr-tests
 * Five-node chain, 0 -> 4. With TTL start 1 and increment 2 the expanding
 * ring needs exactly three floods (TTL 1, 3, 5), and packets sent during the
 * discovery must be delivered once the route exists.
 */
class OdrChainDiscoveryTestCase : public TestCase
{
  public:
    OdrChainDiscoveryTestCase()
        : TestCase("ODR discovers a 4-hop route through an expanding ring search")
    {
    }

  private:
    void DoRun() override
    {
        NodeContainer nodes;
        Ipv4InterfaceContainer ifaces =
            BuildNetwork({{0, 0, 0}, {100, 0, 0}, {200, 0, 0}, {300, 0, 0}, {400, 0, 0}}, nodes);
        UdpSink sink(nodes.Get(4));
        StartUdpFlow(nodes.Get(0), ifaces.GetAddress(4), Seconds(1), MilliSeconds(250), 20);

        Simulator::Schedule(Seconds(6), [this, &nodes, &ifaces]() {
            RoutingTableEntry route;
            bool found = OdrHelper::GetRoutingProtocol(nodes.Get(0))
                             ->LookupRoute(ifaces.GetAddress(4), route);
            NS_TEST_ASSERT_MSG_EQ(found, true, "No route to the chain end");
            NS_TEST_EXPECT_MSG_EQ(route.IsUsable(), true, "Route in use must be valid");
            NS_TEST_EXPECT_MSG_EQ(+route.GetHopCount(), 4, "Wrong hop count");
            NS_TEST_EXPECT_MSG_EQ(route.GetNextHop(), ifaces.GetAddress(1), "Wrong next hop");
        });
        Simulator::Stop(Seconds(10));
        Simulator::Run();

        NS_TEST_EXPECT_MSG_EQ(sink.GetReceived(ifaces.GetAddress(0)), 20U, "Packets lost");
        const Statistics& source = StatsOf(nodes.Get(0));
        NS_TEST_EXPECT_MSG_EQ(source.rreqOriginated, 3U, "Expanding ring should take 3 floods");
        NS_TEST_EXPECT_MSG_EQ(source.discoveriesStarted, 1U, "One discovery expected");
        NS_TEST_EXPECT_MSG_EQ(source.discoveriesSucceeded, 1U, "Discovery did not succeed");
        NS_TEST_EXPECT_MSG_EQ(StatsOf(nodes.Get(4)).rrepOriginated,
                              1U,
                              "Destination must reply once");
        NS_TEST_EXPECT_MSG_EQ(StatsOf(nodes.Get(3)).rrepForwarded, 1U, "RREP not relayed");
        Simulator::Destroy();
    }
};

/**
 * @ingroup odr-tests
 * Node 1 is already routing to node 3 when node 0 starts a discovery for the
 * same destination: node 1 must answer from its cache after a single TTL-1
 * flood, without the destination being involved.
 */
class OdrIntermediateReplyTestCase : public TestCase
{
  public:
    OdrIntermediateReplyTestCase()
        : TestCase("ODR intermediate node answers from a fresh cached route")
    {
    }

  private:
    void DoRun() override
    {
        NodeContainer nodes;
        Ipv4InterfaceContainer ifaces =
            BuildNetwork({{0, 0, 0}, {100, 0, 0}, {200, 0, 0}, {300, 0, 0}}, nodes);
        UdpSink sink(nodes.Get(3));
        StartUdpFlow(nodes.Get(1), ifaces.GetAddress(3), Seconds(1), MilliSeconds(200), 25);
        StartUdpFlow(nodes.Get(0), ifaces.GetAddress(3), Seconds(3.1), MilliSeconds(200), 10);
        Simulator::Stop(Seconds(8));
        Simulator::Run();

        NS_TEST_EXPECT_MSG_EQ(sink.GetReceived(ifaces.GetAddress(1)), 25U, "Flow 1 lost packets");
        NS_TEST_EXPECT_MSG_EQ(sink.GetReceived(ifaces.GetAddress(0)), 10U, "Flow 0 lost packets");
        NS_TEST_EXPECT_MSG_EQ(StatsOf(nodes.Get(0)).rreqOriginated,
                              1U,
                              "A single TTL-1 flood should have sufficed");
        NS_TEST_EXPECT_MSG_EQ(StatsOf(nodes.Get(1)).rrepOriginated,
                              1U,
                              "Node 1 must reply from its cache");
        NS_TEST_EXPECT_MSG_EQ(StatsOf(nodes.Get(3)).rrepOriginated,
                              1U,
                              "The destination must only have answered node 1");

        RoutingTableEntry route;
        OdrHelper::GetRoutingProtocol(nodes.Get(0))->LookupRoute(ifaces.GetAddress(3), route);
        NS_TEST_EXPECT_MSG_EQ(+route.GetHopCount(), 3, "Cached reply must add one hop");
        Simulator::Destroy();
    }
};

/**
 * @ingroup odr-tests
 * Relay 1 leaves at 3.95 s and relay 3 takes its place. The source must
 * detect the break from 802.11 retry exhaustion, rediscover through node 3
 * and resume delivery.
 */
class OdrLinkBreakRepairTestCase : public TestCase
{
  public:
    OdrLinkBreakRepairTestCase()
        : TestCase("ODR detects a MAC-level link break and rediscovers around it")
    {
    }

  private:
    void DoRun() override
    {
        NodeContainer nodes;
        Ipv4InterfaceContainer ifaces =
            BuildNetwork({{0, 0, 0}, {100, 0, 0}, {200, 0, 0}, {100, 1000, 0}}, nodes);
        UdpSink sink(nodes.Get(2));
        StartUdpFlow(nodes.Get(0), ifaces.GetAddress(2), Seconds(1), MilliSeconds(100), 80);
        ScheduleMove(nodes.Get(1), Seconds(3.95), Vector(100, 2000, 0));
        ScheduleMove(nodes.Get(3), Seconds(3.95), Vector(100, 100, 0));

        Simulator::Schedule(Seconds(8), [this, &nodes, &ifaces]() {
            Ptr<RoutingProtocol> odr = OdrHelper::GetRoutingProtocol(nodes.Get(0));
            RoutingTableEntry route;
            odr->LookupRoute(ifaces.GetAddress(2), route);
            NS_TEST_EXPECT_MSG_EQ(route.IsUsable(), true, "No repaired route");
            NS_TEST_EXPECT_MSG_EQ(route.GetNextHop(), ifaces.GetAddress(3), "Old relay kept");
            RoutingTableEntry departed;
            odr->LookupRoute(ifaces.GetAddress(1), departed);
            NS_TEST_EXPECT_MSG_EQ(departed.IsUsable(), false, "Departed neighbor still usable");
        });
        Simulator::Stop(Seconds(10));
        Simulator::Run();

        const Statistics& source = StatsOf(nodes.Get(0));
        NS_TEST_EXPECT_MSG_GT_OR_EQ(source.linkBreaks, 1U, "Break not detected by the MAC hook");
        NS_TEST_EXPECT_MSG_EQ(source.discoveriesSucceeded, 2U, "Expected initial + repair");
        NS_TEST_EXPECT_MSG_GT_OR_EQ(sink.GetReceived(ifaces.GetAddress(0)),
                                    77U,
                                    "Repair lost more than the in-flight packets");
        NS_TEST_EXPECT_MSG_GT_OR_EQ(sink.GetReceivedAfter(Seconds(4.5)),
                                    44U,
                                    "Delivery did not resume after the repair");
        Simulator::Destroy();
    }
};

/**
 * @ingroup odr-tests
 * Chain 0-1-2-3 carrying 0 -> 3; node 3 leaves for good at 3.05 s. Node 2
 * detects the break and the RERR must climb the precursor chain to the
 * source, whose rediscovery then fails and drops the parked packets.
 */
class OdrRerrPropagationTestCase : public TestCase
{
  public:
    OdrRerrPropagationTestCase()
        : TestCase("ODR propagates RERR along precursors and gives up on a partition")
    {
    }

  private:
    void DoRun() override
    {
        NodeContainer nodes;
        Ipv4InterfaceContainer ifaces =
            BuildNetwork({{0, 0, 0}, {100, 0, 0}, {200, 0, 0}, {300, 0, 0}}, nodes);
        UdpSink sink(nodes.Get(3));
        StartUdpFlow(nodes.Get(0), ifaces.GetAddress(3), Seconds(1), MilliSeconds(100), 50);
        ScheduleMove(nodes.Get(3), Seconds(3.05), Vector(300, 5000, 0));

        auto seqBefore = std::make_shared<uint32_t>(0);
        Simulator::Schedule(Seconds(3), [&nodes, &ifaces, seqBefore]() {
            RoutingTableEntry route;
            OdrHelper::GetRoutingProtocol(nodes.Get(0))->LookupRoute(ifaces.GetAddress(3), route);
            *seqBefore = route.GetSeqNo();
        });
        // The first packet after the departure leaves at 3.1 s and exhausts its
        // 802.11 retries at node 2 some 40 ms later. Checking just before the
        // next packet (3.2 s) separates precursor propagation from the RFC 6.11
        // case (ii) fallback, where node 1 would only react to that next packet.
        Simulator::Schedule(Seconds(3.19), [this, &nodes, &ifaces, seqBefore]() {
            RoutingTableEntry route;
            OdrHelper::GetRoutingProtocol(nodes.Get(0))->LookupRoute(ifaces.GetAddress(3), route);
            NS_TEST_EXPECT_MSG_EQ(route.IsUsable(), false, "RERR did not reach the source");
            NS_TEST_EXPECT_MSG_EQ(route.GetSeqNo(),
                                  *seqBefore + 1,
                                  "The bumped seqno must be propagated unchanged");
        });
        Simulator::Stop(Seconds(30));
        Simulator::Run();

        NS_TEST_EXPECT_MSG_EQ(StatsOf(nodes.Get(2)).linkBreaks, 1U, "Break not detected at 2");
        NS_TEST_EXPECT_MSG_EQ(StatsOf(nodes.Get(2)).rerrSent, 1U, "Node 2 did not report");
        NS_TEST_EXPECT_MSG_EQ(StatsOf(nodes.Get(1)).rerrSent, 1U, "Node 1 did not propagate");
        NS_TEST_EXPECT_MSG_EQ(StatsOf(nodes.Get(1)).dataDroppedForwarding,
                              0U,
                              "The source kept using the broken route");
        const Statistics& source = StatsOf(nodes.Get(0));
        NS_TEST_EXPECT_MSG_EQ(source.discoveriesFailed, 1U, "Rediscovery should have failed");
        NS_TEST_EXPECT_MSG_GT(source.dataDroppedNoRoute, 0U, "Parked packets not accounted");
        NS_TEST_EXPECT_MSG_EQ(OdrHelper::GetRoutingProtocol(nodes.Get(0))->GetQueueLength(),
                              0U,
                              "Queue not drained after the failure");
        NS_TEST_EXPECT_MSG_EQ(sink.GetReceivedAfter(Seconds(3.05)), 0U, "Delivery after partition");
        Simulator::Destroy();
    }
};

/**
 * @ingroup odr-tests
 * ODR test suite.
 */
class OdrTestSuite : public TestSuite
{
  public:
    OdrTestSuite()
        : TestSuite("odr", Type::UNIT)
    {
        // The test framework takes ownership of the cases it is given.
        AddTestCase(new OdrHeaderTestCase, Duration::QUICK);
        AddTestCase(new OdrSeqNoTestCase, Duration::QUICK);
        AddTestCase(new OdrRoutingTableTestCase, Duration::QUICK);
        AddTestCase(new OdrRequestCacheTestCase, Duration::QUICK);
        AddTestCase(new OdrPacketQueueTestCase, Duration::QUICK);
        AddTestCase(new OdrChainDiscoveryTestCase, Duration::QUICK);
        AddTestCase(new OdrIntermediateReplyTestCase, Duration::QUICK);
        AddTestCase(new OdrLinkBreakRepairTestCase, Duration::QUICK);
        AddTestCase(new OdrRerrPropagationTestCase, Duration::QUICK);
    }
};

/// Static instance registering the suite with the test runner.
static OdrTestSuite g_odrTestSuite;
