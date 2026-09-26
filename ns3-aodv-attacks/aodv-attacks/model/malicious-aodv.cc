/*
 * malicious-aodv.cc  — see malicious-aodv.h for the design and the
 * honest limitation (data-plane dropping, no forged RREP).
 */
#include "malicious-aodv.h"

#include "ns3/log.h"
#include "ns3/ipv4-interface-address.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MaliciousAodv");
NS_OBJECT_ENSURE_REGISTERED(MaliciousAodv);

TypeId
MaliciousAodv::GetTypeId()
{
    static TypeId tid = TypeId("ns3::MaliciousAodv")
                            .SetParent<Ipv4RoutingProtocol>()
                            .SetGroupName("Internet")
                            .AddConstructor<MaliciousAodv>();
    return tid;
}

MaliciousAodv::MaliciousAodv()
    : m_mode(BLACKHOLE),
      m_grayholeProb(1.0),
      m_startTime(Seconds(0)),
      m_droppedPackets(0),
      m_droppedBytes(0)
{
    m_rand = CreateObject<UniformRandomVariable>();
}

MaliciousAodv::~MaliciousAodv()
{
}

void
MaliciousAodv::SetInner(Ptr<Ipv4RoutingProtocol> inner)
{
    m_inner = inner;
}

void
MaliciousAodv::SetMode(Mode mode)
{
    m_mode = mode;
}

void
MaliciousAodv::SetGrayholeProb(double p)
{
    m_grayholeProb = p;
}

void
MaliciousAodv::SetStartTime(Time t)
{
    m_startTime = t;
}

uint64_t
MaliciousAodv::GetDroppedPackets() const
{
    return m_droppedPackets;
}

uint64_t
MaliciousAodv::GetDroppedBytes() const
{
    return m_droppedBytes;
}

Ptr<Ipv4Route>
MaliciousAodv::RouteOutput(Ptr<Packet> p,
                           const Ipv4Header& header,
                           Ptr<NetDevice> oif,
                           Socket::SocketErrno& sockerr)
{
    // Locally generated packets route normally (the attacker can also be
    // a legitimate source; the attack is only on transit traffic).
    return m_inner->RouteOutput(p, header, oif, sockerr);
}

bool
MaliciousAodv::IsLocalOrBroadcast(Ipv4Address dst) const
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

bool
MaliciousAodv::RouteInput(Ptr<const Packet> p,
                          const Ipv4Header& header,
                          Ptr<const NetDevice> idev,
                          const UnicastForwardCallback& ucb,
                          const MulticastForwardCallback& mcb,
                          const LocalDeliverCallback& lcb,
                          const ErrorCallback& ecb)
{
    Ipv4Address dst = header.GetDestination();

    // Deliver-to-self, broadcast and multicast (incl. AODV control on
    // UDP/654) are always processed by the real AODV.
    bool active = Simulator::Now() >= m_startTime;
    if (!active || IsLocalOrBroadcast(dst))
    {
        return m_inner->RouteInput(p, header, idev, ucb, mcb, lcb, ecb);
    }

    // Transit unicast data packet: this is what the attack targets.
    bool drop = (m_mode == BLACKHOLE) || (m_rand->GetValue(0.0, 1.0) < m_grayholeProb);
    if (drop)
    {
        m_droppedPackets++;
        m_droppedBytes += p->GetSize();
        NS_LOG_DEBUG("Malicious drop of packet to " << dst << " size " << p->GetSize());
        // Consume the packet: report it as handled so nothing else routes it.
        return true;
    }

    // Grayhole "on" packet: forward normally through AODV.
    return m_inner->RouteInput(p, header, idev, ucb, mcb, lcb, ecb);
}

void
MaliciousAodv::NotifyInterfaceUp(uint32_t interface)
{
    m_inner->NotifyInterfaceUp(interface);
}

void
MaliciousAodv::NotifyInterfaceDown(uint32_t interface)
{
    m_inner->NotifyInterfaceDown(interface);
}

void
MaliciousAodv::NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
    m_inner->NotifyAddAddress(interface, address);
}

void
MaliciousAodv::NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
    m_inner->NotifyRemoveAddress(interface, address);
}

void
MaliciousAodv::SetIpv4(Ptr<Ipv4> ipv4)
{
    m_ipv4 = ipv4;
    // The inner AODV had SetIpv4 called at install time; calling it again
    // with the same pointer is safe and keeps both consistent if the L3
    // protocol re-sets the top-level routing protocol.
    if (m_inner)
    {
        m_inner->SetIpv4(ipv4);
    }
}

void
MaliciousAodv::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
    // Show the real table so the attacker looks like a normal AODV node.
    m_inner->PrintRoutingTable(stream, unit);
}

} // namespace ns3
