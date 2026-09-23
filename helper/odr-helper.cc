#include "odr-helper.h"

#include "ns3/abort.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/node.h"

#include <fstream>
#include <iomanip>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OdrHelper");

OdrHelper::OdrHelper()
{
    m_agentFactory.SetTypeId("ns3::odr::RoutingProtocol");
}

OdrHelper*
OdrHelper::Copy() const
{
    return new OdrHelper(*this);
}

Ptr<Ipv4RoutingProtocol>
OdrHelper::Create(Ptr<Node> node) const
{
    Ptr<odr::RoutingProtocol> agent = m_agentFactory.Create<odr::RoutingProtocol>();
    node->AggregateObject(agent);
    return agent;
}

void
OdrHelper::Set(std::string name, const AttributeValue& value)
{
    m_agentFactory.Set(name, value);
}

int64_t
OdrHelper::AssignStreams(NodeContainer nodes, int64_t stream)
{
    int64_t current = stream;
    for (auto it = nodes.Begin(); it != nodes.End(); ++it)
    {
        Ptr<odr::RoutingProtocol> odr = GetRoutingProtocol(*it);
        NS_ABORT_MSG_IF(!odr, "ODR is not installed on node " << (*it)->GetId());
        current += odr->AssignStreams(current);
    }
    return current - stream;
}

Ptr<odr::RoutingProtocol>
OdrHelper::GetRoutingProtocol(Ptr<Node> node)
{
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    if (!ipv4 || !ipv4->GetRoutingProtocol())
    {
        return nullptr;
    }
    return Ipv4RoutingHelper::GetRouting<odr::RoutingProtocol>(ipv4->GetRoutingProtocol());
}

void
OdrHelper::WriteStatistics(const NodeContainer& nodes, const std::string& filename)
{
    std::ofstream out(filename, std::ios::out | std::ios::trunc);
    NS_ABORT_MSG_IF(!out.is_open(), "Cannot open " << filename << " for writing");

    out << "node,rreq_originated,rreq_forwarded,rrep_originated,rrep_forwarded,rerr_sent,"
           "rerr_suppressed,control_tx_packets,control_tx_bytes,control_rx_packets,"
           "control_rx_bytes,discoveries_started,discoveries_succeeded,discoveries_failed,"
           "discovery_latency_sum_s,link_breaks,data_dropped_no_route,data_dropped_queue_full,"
           "data_dropped_queue_timeout,data_dropped_forwarding,data_queued_at_end\n";
    out << std::setprecision(9);
    for (auto it = nodes.Begin(); it != nodes.End(); ++it)
    {
        Ptr<odr::RoutingProtocol> odr = GetRoutingProtocol(*it);
        if (!odr)
        {
            NS_LOG_WARN("Node " << (*it)->GetId() << " has no ODR instance, skipped");
            continue;
        }
        const odr::Statistics& s = odr->GetStatistics();
        out << (*it)->GetId() << ',' << s.rreqOriginated << ',' << s.rreqForwarded << ','
            << s.rrepOriginated << ',' << s.rrepForwarded << ',' << s.rerrSent << ','
            << s.rerrSuppressed << ',' << s.controlTxPackets << ',' << s.controlTxBytes << ','
            << s.controlRxPackets << ',' << s.controlRxBytes << ',' << s.discoveriesStarted << ','
            << s.discoveriesSucceeded << ',' << s.discoveriesFailed << ','
            << s.discoveryLatencySum.GetSeconds() << ',' << s.linkBreaks << ','
            << s.dataDroppedNoRoute << ',' << s.dataDroppedQueueFull << ','
            << s.dataDroppedQueueTimeout << ',' << s.dataDroppedForwarding << ','
            << odr->GetQueueLength() << '\n';
    }
    NS_ABORT_MSG_IF(!out.good(), "Write error on " << filename);
}

} // namespace ns3
