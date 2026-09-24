#include "odr-control-traffic-monitor.h"

#include "ns3/abort.h"
#include "ns3/ipv4-header.h"
#include "ns3/log.h"
#include "ns3/loopback-net-device.h"
#include "ns3/node.h"
#include "ns3/udp-header.h"
#include "ns3/udp-l4-protocol.h"

#include <fstream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OdrControlTrafficMonitor");

namespace odr
{

NS_OBJECT_ENSURE_REGISTERED(ControlTrafficMonitor);

TypeId
ControlTrafficMonitor::GetTypeId()
{
    static TypeId tid = TypeId("ns3::odr::ControlTrafficMonitor")
                            .SetParent<Object>()
                            .SetGroupName("Odr")
                            .AddConstructor<ControlTrafficMonitor>();
    return tid;
}

ControlTrafficMonitor::ControlTrafficMonitor() = default;

void
ControlTrafficMonitor::AddPort(uint16_t port)
{
    m_ports.insert(port);
}

void
ControlTrafficMonitor::Install(NodeContainer nodes)
{
    NS_ABORT_MSG_IF(m_ports.empty(), "Declare the control ports before installing the monitor");
    for (auto it = nodes.Begin(); it != nodes.End(); ++it)
    {
        Ptr<Ipv4> ipv4 = (*it)->GetObject<Ipv4>();
        NS_ABORT_MSG_IF(!ipv4, "Node " << (*it)->GetId() << " has no IPv4 stack");
        uint32_t nodeId = (*it)->GetId();
        m_perNode[nodeId];
        ipv4->TraceConnectWithoutContext("Tx",
                                         MakeCallback(&ControlTrafficMonitor::NotifyTx,
                                                      Ptr<ControlTrafficMonitor>(this),
                                                      nodeId));
    }
}

void
ControlTrafficMonitor::NotifyTx(uint32_t nodeId,
                                Ptr<const Packet> packet,
                                Ptr<Ipv4> ipv4,
                                uint32_t interface)
{
    // Reactive protocols loop packets awaiting a route through the loopback
    // device; those are local detours, not transmissions on the channel.
    if (DynamicCast<LoopbackNetDevice>(ipv4->GetNetDevice(interface)))
    {
        return;
    }
    Ptr<Packet> copy = packet->Copy();
    Ipv4Header ipHeader;
    copy->RemoveHeader(ipHeader);
    // Only the first fragment carries the UDP header. Control messages are far
    // below the MTU, so fragments can only belong to data and are skipped.
    if (ipHeader.GetProtocol() != UdpL4Protocol::PROT_NUMBER || ipHeader.GetFragmentOffset() != 0)
    {
        return;
    }
    UdpHeader udpHeader;
    copy->PeekHeader(udpHeader);
    if (m_ports.count(udpHeader.GetDestinationPort()) == 0 &&
        m_ports.count(udpHeader.GetSourcePort()) == 0)
    {
        return;
    }
    Counters& counters = m_perNode[nodeId];
    ++counters.packets;
    counters.bytes += packet->GetSize();
}

uint64_t
ControlTrafficMonitor::GetTotalPackets() const
{
    uint64_t total = 0;
    for (const auto& [node, counters] : m_perNode)
    {
        total += counters.packets;
    }
    return total;
}

uint64_t
ControlTrafficMonitor::GetTotalBytes() const
{
    uint64_t total = 0;
    for (const auto& [node, counters] : m_perNode)
    {
        total += counters.bytes;
    }
    return total;
}

void
ControlTrafficMonitor::WriteCsv(const std::string& filename) const
{
    std::ofstream out(filename, std::ios::out | std::ios::trunc);
    NS_ABORT_MSG_IF(!out.is_open(), "Cannot open " << filename << " for writing");
    out << "node,control_tx_packets,control_tx_bytes\n";
    for (const auto& [node, counters] : m_perNode)
    {
        out << node << ',' << counters.packets << ',' << counters.bytes << '\n';
    }
    NS_ABORT_MSG_IF(!out.good(), "Write error on " << filename);
}

} // namespace odr
} // namespace ns3
