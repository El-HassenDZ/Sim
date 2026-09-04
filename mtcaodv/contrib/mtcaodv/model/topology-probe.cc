/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "topology-probe.h"

#include "ns3/log.h"
#include "ns3/mobility-model.h"
#include "ns3/simulator.h"

#include <cmath>
#include <queue>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MtcAodvTopologyProbe");

namespace mtcaodv
{

double
FlowConnectivity::GetConnectedFraction() const
{
    return samples ? static_cast<double>(connectedSamples) / samples : std::nan("");
}

double
FlowConnectivity::GetMeanHopCount() const
{
    return connectedSamples ? static_cast<double>(hopSum) / connectedSamples : std::nan("");
}

NS_OBJECT_ENSURE_REGISTERED(TopologyProbe);

TypeId
TopologyProbe::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::mtcaodv::TopologyProbe").SetParent<Object>().SetGroupName("MtcAodv");
    return tid;
}

TopologyProbe::TopologyProbe()
    : m_connectivityRadius(0.0),
      m_interval(Seconds(1.0)),
      m_end(Seconds(0)),
      m_degreeSum(0),
      m_graphSamples(0),
      m_connectedGraphSamples(0)
{
}

TopologyProbe::~TopologyProbe()
{
}

void
TopologyProbe::Start(const NodeContainer& nodes,
                     const std::vector<std::pair<uint32_t, uint32_t>>& flows,
                     double connectivityRadius,
                     Time interval,
                     Time start,
                     Time end)
{
    m_nodes = nodes;
    m_flows = flows;
    m_flowConnectivity.assign(flows.size(), FlowConnectivity());
    m_connectivityRadius = connectivityRadius;
    m_interval = interval;
    m_end = end;

    Simulator::Schedule(start - Simulator::Now(), &TopologyProbe::Sample, this);
}

const std::vector<FlowConnectivity>&
TopologyProbe::GetFlowConnectivity() const
{
    return m_flowConnectivity;
}

double
TopologyProbe::GetMeanDegree() const
{
    if (m_graphSamples == 0 || m_nodes.GetN() == 0)
    {
        return std::nan("");
    }
    return static_cast<double>(m_degreeSum) / (static_cast<double>(m_graphSamples) * m_nodes.GetN());
}

double
TopologyProbe::GetConnectedGraphFraction() const
{
    return m_graphSamples ? static_cast<double>(m_connectedGraphSamples) / m_graphSamples
                          : std::nan("");
}

std::vector<std::vector<uint32_t>>
TopologyProbe::BuildAdjacency() const
{
    const uint32_t count = m_nodes.GetN();
    std::vector<Vector> positions(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        positions[i] = m_nodes.Get(i)->GetObject<MobilityModel>()->GetPosition();
    }

    std::vector<std::vector<uint32_t>> adjacency(count);
    // Comparaison au carré : évite une racine carrée par paire, sans changer le résultat.
    const double squaredRadius = m_connectivityRadius * m_connectivityRadius;

    for (uint32_t i = 0; i < count; ++i)
    {
        for (uint32_t j = i + 1; j < count; ++j)
        {
            const double dx = positions[i].x - positions[j].x;
            const double dy = positions[i].y - positions[j].y;
            const double dz = positions[i].z - positions[j].z;
            if (dx * dx + dy * dy + dz * dz <= squaredRadius)
            {
                adjacency[i].push_back(j);
                adjacency[j].push_back(i);
            }
        }
    }
    return adjacency;
}

int
TopologyProbe::BreadthFirstHopCount(const std::vector<std::vector<uint32_t>>& adjacency,
                                    uint32_t source,
                                    uint32_t destination) const
{
    if (source == destination)
    {
        return 0;
    }

    std::vector<int> distance(adjacency.size(), -1);
    std::queue<uint32_t> pending;
    distance[source] = 0;
    pending.push(source);

    while (!pending.empty())
    {
        const uint32_t current = pending.front();
        pending.pop();
        for (uint32_t neighbor : adjacency[current])
        {
            if (distance[neighbor] < 0)
            {
                distance[neighbor] = distance[current] + 1;
                if (neighbor == destination)
                {
                    return distance[neighbor];
                }
                pending.push(neighbor);
            }
        }
    }
    return -1;
}

void
TopologyProbe::Sample()
{
    const std::vector<std::vector<uint32_t>> adjacency = BuildAdjacency();

    uint64_t degreeSum = 0;
    for (const std::vector<uint32_t>& neighbors : adjacency)
    {
        degreeSum += neighbors.size();
    }
    m_degreeSum += degreeSum;
    ++m_graphSamples;

    // Connexité globale : un parcours depuis le nœud 0 suffit, le graphe étant non orienté.
    if (!adjacency.empty())
    {
        std::vector<bool> visited(adjacency.size(), false);
        std::queue<uint32_t> pending;
        visited[0] = true;
        pending.push(0);
        uint32_t reached = 1;
        while (!pending.empty())
        {
            const uint32_t current = pending.front();
            pending.pop();
            for (uint32_t neighbor : adjacency[current])
            {
                if (!visited[neighbor])
                {
                    visited[neighbor] = true;
                    ++reached;
                    pending.push(neighbor);
                }
            }
        }
        if (reached == adjacency.size())
        {
            ++m_connectedGraphSamples;
        }
    }

    for (size_t flow = 0; flow < m_flows.size(); ++flow)
    {
        const int hops = BreadthFirstHopCount(adjacency, m_flows[flow].first, m_flows[flow].second);
        FlowConnectivity& record = m_flowConnectivity[flow];
        ++record.samples;
        if (hops >= 0)
        {
            ++record.connectedSamples;
            record.hopSum += static_cast<uint64_t>(hops);
            record.maximumHops = std::max(record.maximumHops, static_cast<uint32_t>(hops));
        }
    }

    if (Simulator::Now() + m_interval <= m_end)
    {
        Simulator::Schedule(m_interval, &TopologyProbe::Sample, this);
    }
}

} // namespace mtcaodv
} // namespace ns3
