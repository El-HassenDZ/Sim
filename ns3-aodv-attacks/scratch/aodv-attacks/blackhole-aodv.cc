/*
 * blackhole-aodv.cc — see blackhole-aodv.h for design and honest limits.
 *
 * Reuses ns-3's PUBLIC AODV packet headers only. No src/aodv code is
 * copied or modified.
 */
#include "blackhole-aodv.h"

#include "ns3/aodv-packet.h" // ns3::aodv::TypeHeader / RreqHeader / RrepHeader
#include "ns3/boolean.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-route.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/udp-socket-factory.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("BlackholeAodv");
NS_OBJECT_ENSURE_REGISTERED(BlackholeAodv);

TypeId
BlackholeAodv::GetTypeId()
{
    static TypeId tid = TypeId("ns3::BlackholeAodv")
                            .SetParent<Ipv4RoutingProtocol>()
                            .SetGroupName("Internet")
                            .AddConstructor<BlackholeAodv>();
    return tid;
}

BlackholeAodv::BlackholeAodv()
    : m_startTime(Seconds(0)),
      m_forgedRreps(0),
      m_droppedPackets(0),
      m_droppedBytes(0)
{
}

BlackholeAodv::~BlackholeAodv()
{
}

void
BlackholeAodv::SetStartTime(Time t)
{
    m_startTime = t;
}

uint64_t
BlackholeAodv::GetForgedRreps() const
{
    return m_forgedRreps;
}

uint64_t
BlackholeAodv::GetDroppedPackets() const
{
    return m_droppedPackets;
}

uint64_t
BlackholeAodv::GetDroppedBytes() const
{
    return m_droppedBytes;
}

void
BlackholeAodv::SetIpv4(Ptr<Ipv4> ipv4)
{
    m_ipv4 = ipv4;
    // We replace AODV after the interfaces are already up, so the L3
    // protocol will not replay NotifyInterfaceUp. Set the sockets up
    // ourselves once the simulator starts (mirrors aodv's own Start()).
    Simulator::ScheduleNow(&BlackholeAodv::Start, this);
}

void
BlackholeAodv::Start()
{
    for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); ++i)
    {
        if (m_ipv4->IsUp(i))
        {
            NotifyInterfaceUp(i);
        }
    }
}

bool
BlackholeAodv::IsLocalOrBroadcast(Ipv4Address dst) const
{
    if (dst.IsBroadcast() || dst.IsMulticast() || dst.IsLocalMulticast())
    {
        return true;
    }
    for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); ++i)
    {
        for (uint32_t j = 0; j < m_ipv4->GetNAddresses(i); ++j)
        {
            Ipv4InterfaceAddress ifAddr = m_ipv4->GetAddress(i, j);
            if (dst == ifAddr.GetLocal() || dst == ifAddr.GetBroadcast())
            {
                return true;
            }
        }
    }
    return false;
}

Ptr<Ipv4Route>
BlackholeAodv::LoopbackRoute(const Ipv4Header& header, Ptr<NetDevice> /*oif*/) const
{
    // Minimal on-link route: in an ad-hoc WiFi cell every neighbour is one
    // hop away, so we send directly to the destination via interface 1.
    Ptr<Ipv4Route> route = Create<Ipv4Route>();
    route->SetDestination(header.GetDestination());
    route->SetGateway(header.GetDestination());
    if (m_ipv4->GetNInterfaces() > 1)
    {
        route->SetSource(m_ipv4->GetAddress(1, 0).GetLocal());
        route->SetOutputDevice(m_ipv4->GetNetDevice(1));
    }
    return route;
}

Ptr<Ipv4Route>
BlackholeAodv::RouteOutput(Ptr<Packet> /*p*/,
                           const Ipv4Header& header,
                           Ptr<NetDevice> oif,
                           Socket::SocketErrno& sockerr)
{
    // Only used for packets the attacker itself originates (mainly the
    // forged RREPs, which are sent to on-link neighbours).
    sockerr = Socket::ERROR_NOTERROR;
    return LoopbackRoute(header, oif);
}

bool
BlackholeAodv::RouteInput(Ptr<const Packet> p,
                          const Ipv4Header& header,
                          Ptr<const NetDevice> idev,
                          const UnicastForwardCallback& /*ucb*/,
                          const MulticastForwardCallback& /*mcb*/,
                          const LocalDeliverCallback& lcb,
                          const ErrorCallback& /*ecb*/)
{
    Ipv4Address dst = header.GetDestination();
    int32_t iif = m_ipv4->GetInterfaceForDevice(idev);
    if (iif < 0)
    {
        return false;
    }

    // Deliver packets addressed to us (incl. our own AODV control which is
    // handled separately on the socket). Broadcast/multicast is delivered
    // locally too so the node still "receives" control at L4.
    if (IsLocalOrBroadcast(dst))
    {
        lcb(p, header, static_cast<uint32_t>(iif));
        return true;
    }

    // Transit unicast data: this is the blackhole — drop everything.
    if (Simulator::Now() >= m_startTime)
    {
        m_droppedPackets++;
        m_droppedBytes += p->GetSize();
        NS_LOG_DEBUG("Blackhole drop to " << dst << " size " << p->GetSize());
    }
    return true; // consumed (dropped)
}

void
BlackholeAodv::NotifyInterfaceUp(uint32_t interface)
{
    if (m_ipv4->GetNAddresses(interface) == 0)
    {
        return;
    }
    Ipv4InterfaceAddress iface = m_ipv4->GetAddress(interface, 0);
    if (iface.GetLocal() == Ipv4Address("127.0.0.1"))
    {
        return;
    }
    // Idempotent: do not bind twice to the same interface address.
    for (const auto& kv : m_socketAddresses)
    {
        if (kv.second.GetLocal() == iface.GetLocal())
        {
            return;
        }
    }

    // One socket bound to the interface address, listening on the AODV port,
    // mirroring how aodv::RoutingProtocol sets up its receive sockets.
    Ptr<Socket> socket = Socket::CreateSocket(m_ipv4->GetObject<Node>(),
                                              UdpSocketFactory::GetTypeId());
    NS_ASSERT(socket);
    socket->SetRecvCallback(MakeCallback(&BlackholeAodv::RecvAodv, this));
    socket->BindToNetDevice(m_ipv4->GetNetDevice(interface));
    socket->Bind(InetSocketAddress(iface.GetLocal(), AODV_PORT));
    socket->SetAllowBroadcast(true);
    socket->SetIpRecvTtl(true);
    m_socketAddresses[socket] = iface;
}

void
BlackholeAodv::NotifyInterfaceDown(uint32_t interface)
{
    // Close any socket bound to this interface's address.
    if (m_ipv4->GetNAddresses(interface) == 0)
    {
        return;
    }
    Ipv4InterfaceAddress iface = m_ipv4->GetAddress(interface, 0);
    for (auto it = m_socketAddresses.begin(); it != m_socketAddresses.end();)
    {
        if (it->second.GetLocal() == iface.GetLocal())
        {
            it->first->Close();
            it = m_socketAddresses.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void
BlackholeAodv::NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress /*address*/)
{
    if (m_ipv4->IsUp(interface))
    {
        NotifyInterfaceUp(interface);
    }
}

void
BlackholeAodv::NotifyRemoveAddress(uint32_t /*interface*/, Ipv4InterfaceAddress /*address*/)
{
}

void
BlackholeAodv::RecvAodv(Ptr<Socket> socket)
{
    Address sourceAddress;
    Ptr<Packet> packet = socket->RecvFrom(sourceAddress);
    if (!packet)
    {
        return;
    }
    InetSocketAddress inet = InetSocketAddress::ConvertFrom(sourceAddress);
    Ipv4Address sender = inet.GetIpv4();

    auto it = m_socketAddresses.find(socket);
    if (it == m_socketAddresses.end())
    {
        return;
    }
    Ipv4Address ifaceAddr = it->second.GetLocal();

    // Parse the AODV type header. NS3-VERSION: TypeHeader::Get() returns the
    // MessageType in ns-3.48; older trees used GetType().
    aodv::TypeHeader tHeader;
    packet->RemoveHeader(tHeader);
    if (!tHeader.IsValid())
    {
        return;
    }

    if (tHeader.Get() == aodv::AODVTYPE_RREQ)
    {
        if (Simulator::Now() < m_startTime)
        {
            return; // attack not started: stay silent (do not even reply)
        }
        aodv::RreqHeader rreq;
        packet->RemoveHeader(rreq);
        // Forge a reply claiming the freshest one-hop route to the target.
        SendForgedReply(socket, rreq.GetDst(), rreq.GetDstSeqno(),
                        rreq.GetOrigin(), sender, ifaceAddr);
    }
    // RREP/RERR/HELLO are ignored: a blackhole only needs to answer RREQs.
}

void
BlackholeAodv::SendForgedReply(Ptr<Socket> socket,
                               Ipv4Address requestedDst,
                               uint32_t requestedDstSeqno,
                               Ipv4Address origin,
                               Ipv4Address prevHop,
                               Ipv4Address /*ifaceAddr*/)
{
    // Build an RREP that wins AODV route selection: destination = the
    // requested address, a very high destination sequence number, hop
    // count 1 (we pretend to be one hop from the destination), long
    // lifetime. Origin is the RREQ originator, so honest nodes match it to
    // their pending request and install the route toward us.
    uint32_t dstSeqno = FORGED_SEQNO;
    if (requestedDstSeqno > dstSeqno) // stay strictly fresher than asked
    {
        dstSeqno = requestedDstSeqno + 1;
    }

    aodv::RrepHeader rrep;
    rrep.SetDst(requestedDst);
    rrep.SetDstSeqno(dstSeqno);
    rrep.SetOrigin(origin);
    rrep.SetHopCount(1);
    rrep.SetLifeTime(MilliSeconds(FORGED_LIFETIME_MS));

    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(rrep);
    aodv::TypeHeader tHeader(aodv::AODVTYPE_RREP);
    packet->AddHeader(tHeader);

    // Unicast back to the previous hop (the neighbour that relayed the
    // RREQ); it forwards the RREP along the reverse path to the origin.
    socket->SendTo(packet, 0, InetSocketAddress(prevHop, AODV_PORT));
    m_forgedRreps++;
    NS_LOG_DEBUG("Forged RREP: dst=" << requestedDst << " seqno=" << dstSeqno
                                     << " -> prevHop=" << prevHop);
}

void
BlackholeAodv::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit /*unit*/) const
{
    std::ostream* os = stream->GetStream();
    *os << "Node " << (m_ipv4 ? m_ipv4->GetObject<Node>()->GetId() : 0)
        << " runs BlackholeAodv (active RREP-forging attacker): "
        << "forged RREPs=" << m_forgedRreps << ", dropped packets=" << m_droppedPackets
        << "\n";
}

} // namespace ns3
