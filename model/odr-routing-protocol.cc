#include "odr-routing-protocol.h"

#include "ns3/boolean.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-interface.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/tag.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-mac-header.h"
#include "ns3/wifi-net-device.h"

#include <algorithm>
#include <limits>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OdrRoutingProtocol");

namespace odr
{

NS_OBJECT_ENSURE_REGISTERED(RoutingProtocol);

/**
 * @ingroup odr
 * @brief Marks a locally originated packet looped back while its route is searched.
 *
 * RouteOutput() must answer synchronously, but a discovery takes several
 * round trips. The packet is therefore sent to the loopback device with this
 * tag, re-enters through RouteInput(), and is parked there together with the
 * forwarding callback that will eventually transmit it. The tag also carries
 * the output interface requested by the socket, if any, which the route
 * found later must honour.
 */
class DeferredRouteOutputTag : public Tag
{
  public:
    /**
     * @brief Build the tag.
     * @param oif requested output interface index, or -1 for any
     */
    explicit DeferredRouteOutputTag(int32_t oif = -1)
        : m_oif(oif)
    {
    }

    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId()
    {
        static TypeId tid = TypeId("ns3::odr::DeferredRouteOutputTag")
                                .SetParent<Tag>()
                                .SetGroupName("Odr")
                                .AddConstructor<DeferredRouteOutputTag>();
        return tid;
    }

    TypeId GetInstanceTypeId() const override
    {
        return GetTypeId();
    }

    uint32_t GetSerializedSize() const override
    {
        return sizeof(int32_t);
    }

    void Serialize(TagBuffer i) const override
    {
        i.WriteU32(static_cast<uint32_t>(m_oif));
    }

    void Deserialize(TagBuffer i) override
    {
        m_oif = static_cast<int32_t>(i.ReadU32());
    }

    void Print(std::ostream& os) const override
    {
        os << "DeferredRouteOutputTag oif=" << m_oif;
    }

    /**
     * @brief Get the requested output interface.
     * @return the interface index, or -1 for any
     */
    int32_t GetInterface() const
    {
        return m_oif;
    }

  private:
    int32_t m_oif; //!< Requested output interface, -1 for any
};

NS_OBJECT_ENSURE_REGISTERED(DeferredRouteOutputTag);

TypeId
RoutingProtocol::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::odr::RoutingProtocol")
            .SetParent<Ipv4RoutingProtocol>()
            .SetGroupName("Odr")
            .AddConstructor<RoutingProtocol>()
            .AddAttribute("ActiveRouteTimeout",
                          "Lifetime granted to a route each time it carries data.",
                          TimeValue(Seconds(3)),
                          MakeTimeAccessor(&RoutingProtocol::m_activeRouteTimeout),
                          MakeTimeChecker(MilliSeconds(1)))
            .AddAttribute("NodeTraversalTime",
                          "Conservative one-hop traversal time, queueing and MAC included.",
                          TimeValue(MilliSeconds(40)),
                          MakeTimeAccessor(&RoutingProtocol::m_nodeTraversalTime),
                          MakeTimeChecker(MicroSeconds(1)))
            .AddAttribute("NetDiameter",
                          "Maximum path length in hops; also the TTL of full floods.",
                          UintegerValue(35),
                          MakeUintegerAccessor(&RoutingProtocol::m_netDiameter),
                          MakeUintegerChecker<uint32_t>(1, 255))
            .AddAttribute("RreqRetries",
                          "Additional full-TTL floods before a discovery is abandoned.",
                          UintegerValue(2),
                          MakeUintegerAccessor(&RoutingProtocol::m_rreqRetries),
                          MakeUintegerChecker<uint32_t>(0, 16))
            .AddAttribute("RreqRateLimit",
                          "Maximum RREQ floods originated per second.",
                          UintegerValue(10),
                          MakeUintegerAccessor(&RoutingProtocol::m_rreqRateLimit),
                          MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("RerrRateLimit",
                          "Maximum RERR messages sent per second.",
                          UintegerValue(10),
                          MakeUintegerAccessor(&RoutingProtocol::m_rerrRateLimit),
                          MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("TtlStart",
                          "TTL of the first flood of an expanding ring search.",
                          UintegerValue(1),
                          MakeUintegerAccessor(&RoutingProtocol::m_ttlStart),
                          MakeUintegerChecker<uint32_t>(1, 255))
            .AddAttribute("TtlIncrement",
                          "TTL increase between two floods of the ring search.",
                          UintegerValue(2),
                          MakeUintegerAccessor(&RoutingProtocol::m_ttlIncrement),
                          MakeUintegerChecker<uint32_t>(1, 255))
            .AddAttribute("TtlThreshold",
                          "Largest ring TTL; beyond it the flood uses NetDiameter.",
                          UintegerValue(7),
                          MakeUintegerAccessor(&RoutingProtocol::m_ttlThreshold),
                          MakeUintegerChecker<uint32_t>(1, 255))
            .AddAttribute("TimeoutBuffer",
                          "Extra hops of slack added to ring search timeouts.",
                          UintegerValue(2),
                          MakeUintegerAccessor(&RoutingProtocol::m_timeoutBuffer),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("MaxQueueLength",
                          "Maximum number of data packets parked during discoveries.",
                          UintegerValue(64),
                          MakeUintegerAccessor(&RoutingProtocol::m_maxQueueLength),
                          MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("MaxQueueTime",
                          "Maximum time a data packet may wait for a route.",
                          TimeValue(Seconds(30)),
                          MakeTimeAccessor(&RoutingProtocol::m_maxQueueTime),
                          MakeTimeChecker(MilliSeconds(1)))
            .AddAttribute("DestinationOnly",
                          "Require the destination itself to answer every RREQ.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoutingProtocol::m_destinationOnly),
                          MakeBooleanChecker())
            .AddAttribute("MaxJitter",
                          "Upper bound of the uniform delay applied before a rebroadcast.",
                          TimeValue(MilliSeconds(10)),
                          MakeTimeAccessor(&RoutingProtocol::m_maxJitter),
                          MakeTimeChecker(Time(0)));
    return tid;
}

RoutingProtocol::RoutingProtocol()
    : m_jitter(CreateObject<UniformRandomVariable>()),
      m_seqNo(0),
      m_requestId(0),
      m_rreqWindowCount(0),
      m_rerrWindowCount(0)
{
    m_queue.SetDropCallback(MakeCallback(&RoutingProtocol::OnQueueDrop, this));
}

RoutingProtocol::~RoutingProtocol() = default;

void
RoutingProtocol::DoInitialize()
{
    NS_LOG_FUNCTION(this);
    NS_ABORT_MSG_IF(m_ttlStart > m_netDiameter, "TtlStart exceeds NetDiameter");
    NS_ABORT_MSG_IF(m_ttlThreshold > m_netDiameter, "TtlThreshold exceeds NetDiameter");
    if (m_maxQueueTime < NetTraversalTime())
    {
        NS_LOG_WARN("MaxQueueTime is shorter than NetTraversalTime: packets waiting for a "
                    "full-diameter discovery will expire before its reply can arrive");
    }

    // Sampled once: changing these attributes after the simulation has
    // started has no effect on the helper structures.
    m_routingTable.SetDeletePeriod(DeletePeriod());
    m_requestCache.SetLifetime(PathDiscoveryTime());
    m_queue.SetMaxLength(m_maxQueueLength);
    m_queue.SetMaxDelay(m_maxQueueTime);
    Ipv4RoutingProtocol::DoInitialize();
}

void
RoutingProtocol::DoDispose()
{
    NS_LOG_FUNCTION(this);
    for (auto& [index, discovery] : m_discoveries)
    {
        discovery.timer.Cancel();
    }
    m_discoveries.clear();
    // Teardown order between the IP stack and this object is unspecified, so
    // parked packets are released without calling back into Ipv4L3Protocol.
    m_queue.Clear();
    while (!m_interfaces.empty())
    {
        StopInterface(m_interfaces.begin()->first);
    }
    m_routingTable.Clear();
    m_requestCache.Clear();
    m_ipv4 = nullptr;
    m_lo = nullptr;
    Ipv4RoutingProtocol::DoDispose();
}

int64_t
RoutingProtocol::AssignStreams(int64_t stream)
{
    NS_LOG_FUNCTION(this << stream);
    m_jitter->SetStream(stream);
    return 1;
}

const Statistics&
RoutingProtocol::GetStatistics() const
{
    return m_stats;
}

uint32_t
RoutingProtocol::GetQueueLength()
{
    return m_queue.GetSize();
}

bool
RoutingProtocol::LookupRoute(Ipv4Address dst, RoutingTableEntry& entry) const
{
    return m_routingTable.Lookup(dst, entry);
}

Time
RoutingProtocol::NetTraversalTime() const
{
    return 2 * m_nodeTraversalTime * m_netDiameter;
}

Time
RoutingProtocol::PathDiscoveryTime() const
{
    return 2 * NetTraversalTime();
}

Time
RoutingProtocol::DeletePeriod() const
{
    return 5 * m_activeRouteTimeout;
}

Time
RoutingProtocol::RingTraversalTime(uint32_t ttl) const
{
    return 2 * m_nodeTraversalTime * (ttl + m_timeoutBuffer);
}

void
RoutingProtocol::SetIpv4(Ptr<Ipv4> ipv4)
{
    NS_LOG_FUNCTION(this << ipv4);
    NS_ASSERT(ipv4);
    NS_ASSERT_MSG(!m_ipv4, "ODR is already bound to an IPv4 stack");
    NS_ABORT_MSG_IF(!GetObject<Node>(),
                    "ODR must be aggregated to its node; install it through OdrHelper");
    // Interface 0 is created by Ipv4L3Protocol itself before any routing
    // protocol is attached; the deferred-route mechanism depends on it.
    NS_ABORT_MSG_IF(ipv4->GetNInterfaces() == 0 ||
                        ipv4->GetAddress(0, 0).GetLocal() != Ipv4Address::GetLoopback(),
                    "ODR expects interface 0 to be the loopback interface");
    m_ipv4 = ipv4;
    m_lo = ipv4->GetNetDevice(0);
}

void
RoutingProtocol::NotifyInterfaceUp(uint32_t interface)
{
    NS_LOG_FUNCTION(this << interface);
    StartInterface(interface);
}

void
RoutingProtocol::NotifyInterfaceDown(uint32_t interface)
{
    NS_LOG_FUNCTION(this << interface);
    StopInterface(interface);
    if (m_interfaces.empty())
    {
        m_queue.DropAll(QueueDropReason::SHUTDOWN);
    }
}

void
RoutingProtocol::NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
    NS_LOG_FUNCTION(this << interface << address);
    if (m_ipv4->IsUp(interface) && m_interfaces.count(interface) == 0)
    {
        StartInterface(interface);
    }
}

void
RoutingProtocol::NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
    NS_LOG_FUNCTION(this << interface << address);
    auto it = m_interfaces.find(interface);
    if (it == m_interfaces.end() || it->second.address != address)
    {
        return;
    }
    StopInterface(interface);
    // Another address may remain on the interface; ODR then moves to it.
    if (m_ipv4->IsUp(interface))
    {
        StartInterface(interface);
    }
}

void
RoutingProtocol::StartInterface(uint32_t interface)
{
    if (m_ipv4->GetNAddresses(interface) == 0)
    {
        return;
    }
    Ipv4InterfaceAddress address = m_ipv4->GetAddress(interface, 0);
    if (address.GetLocal() == Ipv4Address::GetLoopback())
    {
        return;
    }
    if (m_ipv4->GetNAddresses(interface) > 1)
    {
        NS_LOG_WARN("ODR runs on the first address of interface " << interface << " only");
    }

    InterfaceState itf;
    itf.address = address;
    itf.device = m_ipv4->GetNetDevice(interface);
    Ptr<Node> node = GetObject<Node>();

    // Two sockets per interface: UDP demultiplexes on the destination address,
    // so a socket bound to the unicast address never sees subnet-directed
    // broadcasts, and binding to the wildcard address would make the sockets of
    // several interfaces collide on ODR_PORT.
    itf.unicastSocket = Socket::CreateSocket(node, UdpSocketFactory::GetTypeId());
    itf.unicastSocket->SetRecvCallback(MakeCallback(&RoutingProtocol::RecvControl, this));
    itf.unicastSocket->BindToNetDevice(itf.device);
    itf.unicastSocket->Bind(InetSocketAddress(address.GetLocal(), ODR_PORT));
    itf.unicastSocket->SetAllowBroadcast(true);
    itf.unicastSocket->SetIpRecvTtl(true);

    itf.broadcastSocket = Socket::CreateSocket(node, UdpSocketFactory::GetTypeId());
    itf.broadcastSocket->SetRecvCallback(MakeCallback(&RoutingProtocol::RecvControl, this));
    itf.broadcastSocket->BindToNetDevice(itf.device);
    itf.broadcastSocket->Bind(InetSocketAddress(address.GetBroadcast(), ODR_PORT));
    itf.broadcastSocket->SetAllowBroadcast(true);
    itf.broadcastSocket->SetIpRecvTtl(true);

    Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol>();
    itf.arpCache = l3->GetInterface(interface)->GetArpCache();

    Ptr<WifiNetDevice> wifi = DynamicCast<WifiNetDevice>(itf.device);
    if (wifi && wifi->GetMac())
    {
        itf.mac = wifi->GetMac();
        itf.mac->TraceConnectWithoutContext("DroppedMpdu",
                                            MakeCallback(&RoutingProtocol::NotifyTxError, this));
    }
    else
    {
        NS_LOG_WARN("Interface " << interface << " offers no link-layer feedback: broken links "
                                 << "are detected only when their routes expire");
    }
    m_interfaces[interface] = itf;
}

void
RoutingProtocol::StopInterface(uint32_t interface)
{
    auto it = m_interfaces.find(interface);
    if (it == m_interfaces.end())
    {
        return;
    }
    InterfaceState& itf = it->second;
    if (itf.mac)
    {
        itf.mac->TraceDisconnectWithoutContext("DroppedMpdu",
                                               MakeCallback(&RoutingProtocol::NotifyTxError, this));
    }
    itf.unicastSocket->Close();
    itf.broadcastSocket->Close();
    m_routingTable.DeleteRoutesOnInterface(itf.address);
    m_interfaces.erase(it);
}

bool
RoutingProtocol::IsMyOwnAddress(Ipv4Address address) const
{
    return std::any_of(m_interfaces.begin(), m_interfaces.end(), [address](const auto& kv) {
        return kv.second.address.GetLocal() == address;
    });
}

bool
RoutingProtocol::IsBroadcast(Ipv4Address dst) const
{
    return dst.IsBroadcast() ||
           std::any_of(m_interfaces.begin(), m_interfaces.end(), [dst](const auto& kv) {
               return kv.second.address.GetBroadcast() == dst;
           });
}

const RoutingProtocol::InterfaceState*
RoutingProtocol::FindInterface(Ptr<Socket> socket) const
{
    for (const auto& [index, itf] : m_interfaces)
    {
        if (itf.unicastSocket == socket || itf.broadcastSocket == socket)
        {
            return &itf;
        }
    }
    return nullptr;
}

Ptr<Ipv4Route>
RoutingProtocol::LoopbackRoute(const Ipv4Header& header, Ptr<NetDevice> oif) const
{
    NS_ASSERT(m_lo);
    NS_ASSERT(!m_interfaces.empty());
    Ptr<Ipv4Route> route = Create<Ipv4Route>();
    route->SetDestination(header.GetDestination());
    // TCP builds its four-tuple and checksum pseudo-header from the source of
    // this provisional route, so it must already be the address the packet
    // will eventually leave from, not 127.0.0.1.
    route->SetSource(m_interfaces.begin()->second.address.GetLocal());
    if (oif)
    {
        for (const auto& [index, itf] : m_interfaces)
        {
            if (itf.device == oif)
            {
                route->SetSource(itf.address.GetLocal());
                break;
            }
        }
    }
    route->SetGateway(Ipv4Address::GetLoopback());
    route->SetOutputDevice(m_lo);
    return route;
}

Ptr<Ipv4Route>
RoutingProtocol::RouteOutput(Ptr<Packet> p,
                             const Ipv4Header& header,
                             Ptr<NetDevice> oif,
                             Socket::SocketErrno& sockerr)
{
    NS_LOG_FUNCTION(this << header << (oif ? oif->GetIfIndex() : 0));
    if (m_interfaces.empty())
    {
        sockerr = Socket::ERROR_NOROUTETOHOST;
        return nullptr;
    }
    sockerr = Socket::ERROR_NOTERROR;
    Ipv4Address dst = header.GetDestination();

    if (IsMyOwnAddress(dst))
    {
        return LoopbackRoute(header, oif);
    }
    if (IsBroadcast(dst))
    {
        // Broadcasts are one-hop only in ODR: sent directly on the requested
        // (or first) interface, never flooded.
        const InterfaceState* chosen = &m_interfaces.begin()->second;
        for (const auto& [index, itf] : m_interfaces)
        {
            if (itf.device == oif || itf.address.GetBroadcast() == dst)
            {
                chosen = &itf;
                break;
            }
        }
        Ptr<Ipv4Route> route = Create<Ipv4Route>();
        route->SetDestination(dst);
        route->SetGateway(Ipv4Address::GetAny());
        route->SetSource(chosen->address.GetLocal());
        route->SetOutputDevice(chosen->device);
        return route;
    }
    if (dst.IsMulticast())
    {
        sockerr = Socket::ERROR_NOROUTETOHOST;
        return nullptr;
    }

    RoutingTableEntry entry;
    if (m_routingTable.LookupUsable(dst, entry))
    {
        if (oif && entry.GetOutputDevice() != oif)
        {
            NS_LOG_DEBUG("Route to " << dst << " leaves through another device than requested");
            sockerr = Socket::ERROR_NOROUTETOHOST;
            return nullptr;
        }
        if (p)
        {
            RefreshActiveRoutes(dst, Ipv4Address::GetAny());
        }
        return entry.GetRoute();
    }

    // A null packet is a route query (e.g. TCP connect): answer with the
    // loopback route so the caller learns its source address, without
    // starting a discovery for traffic that may never be sent.
    if (p)
    {
        DeferredRouteOutputTag tag(oif ? m_ipv4->GetInterfaceForDevice(oif) : -1);
        if (!p->PeekPacketTag(tag))
        {
            p->AddPacketTag(tag);
        }
    }
    return LoopbackRoute(header, oif);
}

bool
RoutingProtocol::RouteInput(Ptr<const Packet> p,
                            const Ipv4Header& header,
                            Ptr<const NetDevice> idev,
                            const UnicastForwardCallback& ucb,
                            const MulticastForwardCallback& mcb,
                            const LocalDeliverCallback& lcb,
                            const ErrorCallback& ecb)
{
    NS_LOG_FUNCTION(this << p->GetUid() << header.GetDestination() << idev->GetAddress());
    if (m_interfaces.empty())
    {
        return false;
    }
    int32_t iif = m_ipv4->GetInterfaceForDevice(idev);
    NS_ASSERT_MSG(iif >= 0, "Packet received on a device without IPv4");
    Ipv4Address dst = header.GetDestination();
    Ipv4Address origin = header.GetSource();

    if (idev == m_lo)
    {
        DeferredRouteOutputTag tag;
        if (p->PeekPacketTag(tag))
        {
            DeferRouteOutput(p, header, ucb, ecb);
            return true;
        }
    }
    if (dst.IsMulticast())
    {
        return false;
    }
    // A packet of ours coming back from the network is either the echo of our
    // own broadcast or a unicast caught in a routing loop; neither may be
    // delivered or forwarded again.
    if (IsMyOwnAddress(origin) && idev != m_lo)
    {
        NS_LOG_LOGIC("Discard own packet " << p->GetUid() << " received back from the network");
        return true;
    }

    if (IsBroadcast(dst) || m_ipv4->IsDestinationAddress(dst, iif))
    {
        if (lcb.IsNull())
        {
            ecb(p, header, Socket::ERROR_NOROUTETOHOST);
            return true;
        }
        if (!IsBroadcast(dst))
        {
            // Traffic from a source proves the reverse path is still in use;
            // keeping it alive spares a discovery for replies or TCP ACKs.
            RefreshActiveRoutes(origin, Ipv4Address::GetAny());
        }
        lcb(p, header, iif);
        return true;
    }

    if (!m_ipv4->IsForwarding(iif))
    {
        ecb(p, header, Socket::ERROR_NOROUTETOHOST);
        return true;
    }
    return Forward(p, header, ucb);
}

bool
RoutingProtocol::Forward(Ptr<const Packet> p,
                         const Ipv4Header& header,
                         const UnicastForwardCallback& ucb)
{
    Ipv4Address dst = header.GetDestination();
    RoutingTableEntry toDst;
    if (m_routingTable.LookupUsable(dst, toDst))
    {
        RefreshActiveRoutes(dst, header.GetSource());
        ucb(toDst.GetRoute(), p, header);
        return true;
    }

    // RFC 3561 section 6.11 case (ii): the upstream node still believes this
    // node leads to dst. The RERR goes out as a one-hop broadcast because the
    // previous hop is not known from the IP header, and only neighbors whose
    // route to dst uses this node act upon it.
    NS_LOG_DEBUG("No route to forward packet " << p->GetUid() << " to " << dst);
    ++m_stats.dataDroppedForwarding;
    uint32_t seqNo = 0;
    if (RoutingTableEntry* stale = m_routingTable.Find(dst))
    {
        // The number is bumped only on the VALID -> INVALID transition. Bumping
        // it for every stranded packet would inflate it past the destination's
        // real value and make this node refuse legitimate cached replies.
        if (stale->GetState() == RouteState::VALID)
        {
            if (stale->HasValidSeqNo())
            {
                stale->SetSeqNo(stale->GetSeqNo() + 1);
            }
            stale->Invalidate(DeletePeriod());
        }
        seqNo = stale->GetSeqNo();
    }
    SendError({{dst, seqNo}}, {});
    return false;
}

void
RoutingProtocol::RefreshActiveRoutes(Ipv4Address dst, Ipv4Address origin)
{
    // RFC 3561 section 6.2: a route in use, its next hop, and the reverse path
    // towards the source all stay alive for ActiveRouteTimeout.
    auto refresh = [this](Ipv4Address address) {
        RoutingTableEntry* entry = m_routingTable.Find(address);
        if (entry && entry->IsUsable())
        {
            entry->Refresh(m_activeRouteTimeout);
            return entry;
        }
        return static_cast<RoutingTableEntry*>(nullptr);
    };
    if (RoutingTableEntry* toDst = refresh(dst))
    {
        refresh(toDst->GetNextHop());
    }
    if (origin != Ipv4Address::GetAny() && !IsMyOwnAddress(origin))
    {
        if (RoutingTableEntry* toOrigin = refresh(origin))
        {
            refresh(toOrigin->GetNextHop());
        }
    }
}

void
RoutingProtocol::DeferRouteOutput(Ptr<const Packet> p,
                                  const Ipv4Header& header,
                                  const UnicastForwardCallback& ucb,
                                  const ErrorCallback& ecb)
{
    Ipv4Address dst = header.GetDestination();
    QueueEntry entry(p, header, ucb, ecb, Simulator::Now() + m_queue.GetMaxDelay());
    if (!m_queue.Enqueue(entry))
    {
        return;
    }
    // The route may have appeared during the loopback round trip, e.g. from a
    // RREP processed in between; parking the packet until the next discovery
    // would then delay it for nothing.
    RoutingTableEntry toDst;
    if (m_routingTable.LookupUsable(dst, toDst))
    {
        SendQueuedPackets(dst);
        return;
    }
    StartDiscovery(dst);
}

void
RoutingProtocol::OnRouteAvailable(Ipv4Address dst)
{
    auto it = m_discoveries.find(dst);
    if (it != m_discoveries.end())
    {
        it->second.timer.Cancel();
        ++m_stats.discoveriesSucceeded;
        m_stats.discoveryLatencySum += Simulator::Now() - it->second.started;
        m_discoveries.erase(it);
    }
    SendQueuedPackets(dst);
}

void
RoutingProtocol::SendQueuedPackets(Ipv4Address dst)
{
    RoutingTableEntry toDst;
    if (!m_routingTable.LookupUsable(dst, toDst))
    {
        return;
    }
    int32_t outInterface = m_ipv4->GetInterfaceForDevice(toDst.GetOutputDevice());
    for (const QueueEntry& entry : m_queue.Dequeue(dst))
    {
        // The looped-back packet still carries the deferral tag; it must not
        // travel with the packet, where a downstream node receiving it on its
        // own loopback (e.g. a local delivery) would park it again.
        Ptr<Packet> packet = entry.GetPacket()->Copy();
        DeferredRouteOutputTag tag;
        if (packet->RemovePacketTag(tag) && tag.GetInterface() != -1 &&
            tag.GetInterface() != outInterface)
        {
            NS_LOG_DEBUG("Route to " << dst << " does not use the interface bound by the socket");
            OnQueueDrop(entry, QueueDropReason::NO_ROUTE);
            if (!entry.GetErrorCallback().IsNull())
            {
                entry.GetErrorCallback()(entry.GetPacket(),
                                         entry.GetIpv4Header(),
                                         Socket::ERROR_NOROUTETOHOST);
            }
            continue;
        }
        RefreshActiveRoutes(dst, Ipv4Address::GetAny());
        entry.GetUnicastForwardCallback()(toDst.GetRoute(), packet, entry.GetIpv4Header());
    }
}

void
RoutingProtocol::OnQueueDrop(const QueueEntry& entry, QueueDropReason reason)
{
    switch (reason)
    {
    case QueueDropReason::QUEUE_FULL:
        ++m_stats.dataDroppedQueueFull;
        break;
    case QueueDropReason::EXPIRED:
        ++m_stats.dataDroppedQueueTimeout;
        break;
    case QueueDropReason::NO_ROUTE:
    case QueueDropReason::SHUTDOWN:
        ++m_stats.dataDroppedNoRoute;
        break;
    }
}

bool
RoutingProtocol::ConsumeToken(Time& windowStart, uint32_t& count, uint32_t limit)
{
    // A lazily restarted window replaces the periodic reset timer of the RFC
    // pseudo-code: idle nodes then schedule no event at all.
    Time now = Simulator::Now();
    if (now - windowStart >= Seconds(1))
    {
        windowStart = now;
        count = 0;
    }
    if (count >= limit)
    {
        return false;
    }
    ++count;
    return true;
}

void
RoutingProtocol::StartDiscovery(Ipv4Address dst)
{
    if (m_discoveries.count(dst) != 0)
    {
        return;
    }
    Discovery discovery;
    discovery.started = Simulator::Now();
    // RFC 3561 section 6.4: restart the ring just beyond the last known
    // distance, since the destination has probably not moved far.
    RoutingTableEntry stale;
    if (m_routingTable.Lookup(dst, stale) && stale.GetHopCount() > 0)
    {
        discovery.ttl = std::min(stale.GetHopCount() + m_ttlIncrement, m_netDiameter);
    }
    else
    {
        discovery.ttl = m_ttlStart;
    }
    if (discovery.ttl > m_ttlThreshold)
    {
        discovery.ttl = m_netDiameter;
    }
    m_discoveries[dst] = discovery;
    ++m_stats.discoveriesStarted;
    SendRequest(dst);
}

void
RoutingProtocol::SendRequest(Ipv4Address dst)
{
    auto it = m_discoveries.find(dst);
    if (it == m_discoveries.end())
    {
        return;
    }
    Discovery& discovery = it->second;
    if (!ConsumeToken(m_rreqWindowStart, m_rreqWindowCount, m_rreqRateLimit))
    {
        Time wait = m_rreqWindowStart + Seconds(1) - Simulator::Now();
        NS_LOG_DEBUG("RREQ rate limit reached, flood for " << dst << " delayed by " << wait);
        discovery.timer = Simulator::Schedule(wait, &RoutingProtocol::SendRequest, this, dst);
        return;
    }

    // RFC 3561 section 6.1: the originator's number must grow before each
    // flood, otherwise nodes that already hold a reverse route from an earlier
    // flood would refuse to update it towards the originator's new position.
    ++m_seqNo;
    RreqHeader rreq;
    rreq.SetRequestId(++m_requestId);
    rreq.SetDestination(dst);
    rreq.SetOriginSeqNo(m_seqNo);
    rreq.SetDestinationOnly(m_destinationOnly);
    RoutingTableEntry known;
    if (m_routingTable.Lookup(dst, known) && known.HasValidSeqNo())
    {
        rreq.SetDestinationSeqNo(known.GetSeqNo());
    }
    else
    {
        rreq.SetUnknownSeqNo(true);
    }

    for (const auto& [index, itf] : m_interfaces)
    {
        rreq.SetOrigin(itf.address.GetLocal());
        // Registering our own flood suppresses the copies neighbors echo back.
        m_requestCache.IsDuplicate(rreq.GetOrigin(), rreq.GetRequestId());
        Ptr<Packet> packet = Create<Packet>();
        packet->AddHeader(rreq);
        packet->AddHeader(TypeHeader(ODR_RREQ));
        if (SendControl(itf,
                        packet,
                        itf.address.GetBroadcast(),
                        static_cast<uint8_t>(discovery.ttl)))
        {
            ++m_stats.rreqOriginated;
        }
    }

    Time timeout = discovery.ttl < m_netDiameter ? RingTraversalTime(discovery.ttl)
                                                 : NetTraversalTime() * (1U << discovery.retries);
    NS_LOG_DEBUG("RREQ " << m_requestId << " for " << dst << " ttl " << discovery.ttl << " timeout "
                         << timeout);
    discovery.timer = Simulator::Schedule(timeout, &RoutingProtocol::OnDiscoveryTimeout, this, dst);
}

void
RoutingProtocol::OnDiscoveryTimeout(Ipv4Address dst)
{
    auto it = m_discoveries.find(dst);
    NS_ASSERT_MSG(it != m_discoveries.end(), "Timeout of a discovery that no longer exists");
    Discovery& discovery = it->second;

    RoutingTableEntry toDst;
    if (m_routingTable.LookupUsable(dst, toDst))
    {
        OnRouteAvailable(dst);
        return;
    }
    if (discovery.ttl < m_netDiameter)
    {
        discovery.ttl += m_ttlIncrement;
        if (discovery.ttl > m_ttlThreshold)
        {
            discovery.ttl = m_netDiameter;
        }
        SendRequest(dst);
        return;
    }
    if (discovery.retries < m_rreqRetries)
    {
        ++discovery.retries;
        SendRequest(dst);
        return;
    }

    // Partition or departed destination. The discovery is erased before the
    // parked packets are dropped: the drop callbacks run user trace sinks,
    // which may legitimately send again and must be allowed a fresh discovery.
    NS_LOG_DEBUG("Discovery for " << dst << " abandoned");
    ++m_stats.discoveriesFailed;
    m_discoveries.erase(it);
    m_queue.Drop(dst, QueueDropReason::NO_ROUTE);
}

void
RoutingProtocol::RecvControl(Ptr<Socket> socket)
{
    Address from;
    Ptr<Packet> packet = socket->RecvFrom(from);
    Ipv4Address sender = InetSocketAddress::ConvertFrom(from).GetIpv4();
    const InterfaceState* itf = FindInterface(socket);
    NS_ASSERT_MSG(itf, "Control message on a socket ODR does not own");
    if (IsMyOwnAddress(sender))
    {
        return;
    }
    ++m_stats.controlRxPackets;
    m_stats.controlRxBytes += packet->GetSize();

    SocketIpTtlTag ttlTag;
    uint8_t ttl = packet->RemovePacketTag(ttlTag) ? ttlTag.GetTtl() : 1;

    TypeHeader type;
    packet->RemoveHeader(type);
    if (!type.IsValid())
    {
        return;
    }

    // Any control message proves the sender is a neighbor right now. This is
    // also what makes the unicast replies below routable: the RREP back to the
    // sender needs a route to it before the reverse route is even examined.
    if (m_routingTable.RefreshNeighbor(itf->device, itf->address, sender, m_activeRouteTimeout))
    {
        OnRouteAvailable(sender);
    }

    switch (type.GetType())
    {
    case ODR_RREQ:
        RecvRequest(packet, *itf, sender, ttl);
        break;
    case ODR_RREP:
        RecvReply(packet, *itf, sender);
        break;
    case ODR_RERR:
        RecvError(packet, sender);
        break;
    }
}

void
RoutingProtocol::RecvRequest(Ptr<Packet> packet,
                             const InterfaceState& itf,
                             Ipv4Address sender,
                             uint8_t ttl)
{
    RreqHeader rreq;
    packet->RemoveHeader(rreq);
    Ipv4Address origin = rreq.GetOrigin();
    Ipv4Address dst = rreq.GetDestination();
    if (IsMyOwnAddress(origin) || m_requestCache.IsDuplicate(origin, rreq.GetRequestId()))
    {
        return;
    }
    if (rreq.GetHopCount() == std::numeric_limits<uint8_t>::max())
    {
        NS_LOG_WARN("RREQ from " << origin << " reached the 8-bit hop count limit");
        return;
    }
    uint8_t hops = rreq.GetHopCount() + 1;

    // RFC 3561 section 6.5 minimal lifetime of a reverse route: long enough
    // for a reply to cross the rest of the network and come back. The floor at
    // ActiveRouteTimeout keeps it positive when hops approach NetDiameter.
    Time reverseLifetime = 2 * NetTraversalTime() - 2 * hops * m_nodeTraversalTime;
    RoutingTableEntry reverse(itf.device,
                              itf.address,
                              origin,
                              sender,
                              hops,
                              Simulator::Now() + std::max(reverseLifetime, m_activeRouteTimeout));
    reverse.SetSeqNo(rreq.GetOriginSeqNo());
    if (m_routingTable.Update(reverse))
    {
        OnRouteAvailable(origin);
    }

    if (IsMyOwnAddress(dst))
    {
        ReplyAsDestination(rreq, sender);
        return;
    }

    RoutingTableEntry toDst;
    bool known = m_routingTable.Lookup(dst, toDst);
    // A cached reply whose next hop is the requester itself would advertise
    // the path requester -> self -> requester; the requester is looking
    // because its own route broke, so it is never useful and risks a loop if
    // the requester's route merely timed out without a sequence number bump.
    if (!m_destinationOnly && !rreq.IsDestinationOnly() && known && toDst.IsUsable() &&
        toDst.HasValidSeqNo() && toDst.GetNextHop() != sender &&
        (rreq.IsUnknownSeqNo() || !IsFresher(rreq.GetDestinationSeqNo(), toDst.GetSeqNo())))
    {
        ReplyFromCache(rreq, toDst, sender);
        return;
    }

    if (ttl <= 1)
    {
        return;
    }
    rreq.SetHopCount(hops);
    if (known && toDst.HasValidSeqNo() &&
        (rreq.IsUnknownSeqNo() || IsFresher(toDst.GetSeqNo(), rreq.GetDestinationSeqNo())))
    {
        rreq.SetDestinationSeqNo(toDst.GetSeqNo());
        rreq.SetUnknownSeqNo(false);
    }
    Ptr<Packet> forward = Create<Packet>();
    forward->AddHeader(rreq);
    forward->AddHeader(TypeHeader(ODR_RREQ));
    // Neighbors hearing the same broadcast would otherwise rebroadcast in the
    // same slot; 802.11 broadcasts have neither RTS/CTS nor ACK, so those
    // collisions are silent and stall the flood (broadcast storm problem).
    Time jitter = Seconds(m_jitter->GetValue(0, m_maxJitter.GetSeconds()));
    Simulator::Schedule(jitter,
                        &RoutingProtocol::BroadcastControl,
                        this,
                        forward,
                        static_cast<uint8_t>(ttl - 1),
                        &Statistics::rreqForwarded);
}

void
RoutingProtocol::ReplyAsDestination(const RreqHeader& rreq, Ipv4Address sender)
{
    // RFC 3561 section 6.6.1: never answer with a number older than the one
    // the requester already knows, or its update rule would discard the reply.
    if (!rreq.IsUnknownSeqNo() && IsFresher(rreq.GetDestinationSeqNo(), m_seqNo))
    {
        m_seqNo = rreq.GetDestinationSeqNo();
    }
    RrepHeader rrep;
    rrep.SetHopCount(0);
    rrep.SetDestination(rreq.GetDestination());
    rrep.SetDestinationSeqNo(m_seqNo);
    rrep.SetOrigin(rreq.GetOrigin());
    rrep.SetLifetime(2 * m_activeRouteTimeout);

    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(rrep);
    packet->AddHeader(TypeHeader(ODR_RREP));
    if (UnicastControl(packet, sender))
    {
        ++m_stats.rrepOriginated;
    }
}

void
RoutingProtocol::ReplyFromCache(const RreqHeader& rreq,
                                const RoutingTableEntry& toDst,
                                Ipv4Address sender)
{
    RrepHeader rrep;
    rrep.SetHopCount(toDst.GetHopCount());
    rrep.SetDestination(toDst.GetDestination());
    rrep.SetDestinationSeqNo(toDst.GetSeqNo());
    rrep.SetOrigin(rreq.GetOrigin());
    rrep.SetLifetime(toDst.GetExpiry() - Simulator::Now());

    // Both directions now rely on this node: the requester side through the
    // forward route, the destination side through the reverse route.
    m_routingTable.Find(toDst.GetDestination())->AddPrecursor(sender);
    if (RoutingTableEntry* toOrigin = m_routingTable.Find(rreq.GetOrigin()))
    {
        toOrigin->AddPrecursor(toDst.GetNextHop());
    }

    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(rrep);
    packet->AddHeader(TypeHeader(ODR_RREP));
    if (UnicastControl(packet, sender))
    {
        ++m_stats.rrepOriginated;
    }
}

void
RoutingProtocol::RecvReply(Ptr<Packet> packet, const InterfaceState& itf, Ipv4Address sender)
{
    RrepHeader rrep;
    packet->RemoveHeader(rrep);
    Ipv4Address dst = rrep.GetDestination();
    Ipv4Address origin = rrep.GetOrigin();
    if (IsMyOwnAddress(dst))
    {
        return;
    }
    if (rrep.GetHopCount() == std::numeric_limits<uint8_t>::max())
    {
        NS_LOG_WARN("RREP for " << dst << " reached the 8-bit hop count limit");
        return;
    }
    uint8_t hops = rrep.GetHopCount() + 1;

    RoutingTableEntry forward(itf.device,
                              itf.address,
                              dst,
                              sender,
                              hops,
                              Simulator::Now() + rrep.GetLifetime());
    forward.SetSeqNo(rrep.GetDestinationSeqNo());
    bool updated = m_routingTable.Update(forward);

    if (IsMyOwnAddress(origin))
    {
        RoutingTableEntry toDst;
        if (m_routingTable.LookupUsable(dst, toDst))
        {
            OnRouteAvailable(dst);
        }
        return;
    }
    // RFC 3561 section 6.7: only a reply that changed our table is relayed. A
    // reply we rejected advertises a route no better than ours, and the
    // originator will get ours if it is part of the path.
    if (!updated)
    {
        return;
    }
    OnRouteAvailable(dst);

    RoutingTableEntry toOrigin;
    if (!m_routingTable.LookupUsable(origin, toOrigin))
    {
        // The reverse route timed out or was invalidated by a RERR while the
        // reply travelled: the originator will retry with a new flood.
        NS_LOG_DEBUG("RREP for " << dst << " stranded: no reverse route to " << origin);
        return;
    }
    m_routingTable.Find(dst)->AddPrecursor(toOrigin.GetNextHop());
    m_routingTable.Find(sender)->AddPrecursor(toOrigin.GetNextHop());
    RoutingTableEntry* reverse = m_routingTable.Find(origin);
    reverse->AddPrecursor(sender);
    reverse->Refresh(m_activeRouteTimeout);

    rrep.SetHopCount(hops);
    Ptr<Packet> relay = Create<Packet>();
    relay->AddHeader(rrep);
    relay->AddHeader(TypeHeader(ODR_RREP));
    if (UnicastControl(relay, toOrigin.GetNextHop()))
    {
        ++m_stats.rrepForwarded;
    }
}

void
RoutingProtocol::RecvError(Ptr<Packet> packet, Ipv4Address sender)
{
    RerrHeader rerr;
    packet->RemoveHeader(rerr);
    if (rerr.GetDestinationCount() == 0)
    {
        return;
    }
    std::set<Ipv4Address> precursors;
    std::vector<UnreachableDestination> propagated =
        m_routingTable.InvalidateReported(sender, rerr.GetUnreachable(), precursors);
    // Propagation stops where no route used the sender as next hop, which
    // bounds a RERR to the upstream subtree of the broken link.
    if (!propagated.empty() && !precursors.empty())
    {
        SendError(propagated, std::move(precursors));
    }
}

void
RoutingProtocol::NotifyTxError(WifiMacDropReason reason, Ptr<const WifiMpdu> mpdu)
{
    // Only retry exhaustion says the receiver is gone. Queue overflow or MSDU
    // lifetime expiry happen under congestion with the link intact; treating
    // them as breaks would tear down working routes exactly when the network
    // is loaded and flood it with rediscoveries.
    if (reason != WIFI_MAC_DROP_REACHED_RETRY_LIMIT)
    {
        return;
    }
    Mac48Address receiver = mpdu->GetHeader().GetAddr1();
    if (receiver.IsGroup())
    {
        return;
    }
    std::set<Ipv4Address> lost;
    for (const auto& [index, itf] : m_interfaces)
    {
        if (!itf.arpCache)
        {
            continue;
        }
        for (ArpCache::Entry* arp : itf.arpCache->LookupInverse(receiver))
        {
            lost.insert(arp->GetIpv4Address());
        }
    }
    for (Ipv4Address neighbor : lost)
    {
        HandleLinkFailure(neighbor);
    }
}

void
RoutingProtocol::HandleLinkFailure(Ipv4Address nextHop)
{
    NS_LOG_DEBUG("Link to " << nextHop << " lost");
    std::set<Ipv4Address> precursors;
    std::vector<UnreachableDestination> unreachable =
        m_routingTable.InvalidateByNextHop(nextHop, precursors);
    if (unreachable.empty())
    {
        return;
    }
    ++m_stats.linkBreaks;
    if (!precursors.empty())
    {
        SendError(unreachable, std::move(precursors));
    }
}

void
RoutingProtocol::SendError(const std::vector<UnreachableDestination>& unreachable,
                           std::set<Ipv4Address> precursors)
{
    for (auto it = precursors.begin(); it != precursors.end();)
    {
        it = IsMyOwnAddress(*it) ? precursors.erase(it) : std::next(it);
    }

    for (std::size_t first = 0; first < unreachable.size(); first += RerrHeader::MAX_DESTINATIONS)
    {
        if (!ConsumeToken(m_rerrWindowStart, m_rerrWindowCount, m_rerrRateLimit))
        {
            // Dropping rather than delaying: a late RERR describes a topology
            // that has moved on, and upstream nodes detect the break anyway
            // when their own transmissions fail.
            ++m_stats.rerrSuppressed;
            continue;
        }
        RerrHeader rerr;
        std::size_t last = std::min(unreachable.size(), first + RerrHeader::MAX_DESTINATIONS);
        for (std::size_t k = first; k < last; ++k)
        {
            rerr.AddUnreachable(unreachable[k].address, unreachable[k].seqNo);
        }
        Ptr<Packet> packet = Create<Packet>();
        packet->AddHeader(rerr);
        packet->AddHeader(TypeHeader(ODR_RERR));

        // RFC 3561 section 6.11: unicast suffices for a single precursor;
        // otherwise one broadcast reaches them all for the price of one frame.
        if (precursors.size() == 1 && UnicastControl(packet, *precursors.begin()))
        {
            ++m_stats.rerrSent;
            continue;
        }
        BroadcastControl(packet, 1, &Statistics::rerrSent);
    }
}

void
RoutingProtocol::BroadcastControl(Ptr<Packet> packet, uint8_t ttl, uint64_t Statistics::* counter)
{
    for (const auto& [index, itf] : m_interfaces)
    {
        if (SendControl(itf, packet->Copy(), itf.address.GetBroadcast(), ttl))
        {
            ++(m_stats.*counter);
        }
    }
}

bool
RoutingProtocol::UnicastControl(Ptr<Packet> packet, Ipv4Address neighbor)
{
    // The socket send path calls RouteOutput(); without a usable route the
    // message would be parked as data and trigger a discovery for a neighbor,
    // so the route is checked here and the message dropped instead.
    RoutingTableEntry toNeighbor;
    if (!m_routingTable.LookupUsable(neighbor, toNeighbor))
    {
        NS_LOG_DEBUG("No route to neighbor " << neighbor << ", control message dropped");
        return false;
    }
    int32_t index = m_ipv4->GetInterfaceForDevice(toNeighbor.GetOutputDevice());
    auto it = m_interfaces.find(static_cast<uint32_t>(index));
    NS_ASSERT_MSG(it != m_interfaces.end(), "Route through an interface ODR does not run on");
    return SendControl(it->second, packet, neighbor, 1);
}

bool
RoutingProtocol::SendControl(const InterfaceState& itf,
                             Ptr<Packet> packet,
                             Ipv4Address to,
                             uint8_t ttl)
{
    uint32_t size = packet->GetSize();
    SocketIpTtlTag tag;
    tag.SetTtl(ttl);
    packet->AddPacketTag(tag);
    if (itf.unicastSocket->SendTo(packet, 0, InetSocketAddress(to, ODR_PORT)) < 0)
    {
        NS_LOG_WARN("Control message to " << to << " refused by the socket, errno "
                                          << itf.unicastSocket->GetErrno());
        return false;
    }
    ++m_stats.controlTxPackets;
    m_stats.controlTxBytes += size;
    return true;
}

void
RoutingProtocol::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
    Ptr<Node> node = m_ipv4->GetObject<Node>();
    *stream->GetStream() << "Node: " << node->GetId() << "; Time: " << Now().As(unit)
                         << ", Local time: " << node->GetLocalTime().As(unit)
                         << ", ODR routing table\n";
    m_routingTable.Print(stream, unit);
    *stream->GetStream() << "\n";
}

} // namespace odr
} // namespace ns3
