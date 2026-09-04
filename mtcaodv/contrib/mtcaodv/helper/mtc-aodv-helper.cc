/*
 * Copyright (c) 2009 IITP RAS
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Pavel Boyko <boyko@iitp.ru>, written after OlsrHelper by Mathieu Lacage
 * <mathieu.lacage@sophia.inria.fr>
 */
#include "mtc-aodv-helper.h"

#include "ns3/mtc-aodv-routing-protocol.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/names.h"
#include "ns3/node-list.h"
#include "ns3/ptr.h"

namespace ns3
{

MtcAodvHelper::MtcAodvHelper()
    : Ipv4RoutingHelper()
{
    m_agentFactory.SetTypeId("ns3::mtcaodv::RoutingProtocol");
}

MtcAodvHelper*
MtcAodvHelper::Copy() const
{
    return new MtcAodvHelper(*this);
}

Ptr<Ipv4RoutingProtocol>
MtcAodvHelper::Create(Ptr<Node> node) const
{
    Ptr<mtcaodv::RoutingProtocol> agent = m_agentFactory.Create<mtcaodv::RoutingProtocol>();
    node->AggregateObject(agent);
    return agent;
}

void
MtcAodvHelper::Set(std::string name, const AttributeValue& value)
{
    m_agentFactory.Set(name, value);
}

int64_t
MtcAodvHelper::AssignStreams(NodeContainer c, int64_t stream)
{
    int64_t currentStream = stream;
    Ptr<Node> node;
    for (auto i = c.Begin(); i != c.End(); ++i)
    {
        node = (*i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        NS_ASSERT_MSG(ipv4, "Ipv4 not installed on node");
        Ptr<Ipv4RoutingProtocol> proto = ipv4->GetRoutingProtocol();
        NS_ASSERT_MSG(proto, "Ipv4 routing not installed on node");
        Ptr<mtcaodv::RoutingProtocol> aodv = DynamicCast<mtcaodv::RoutingProtocol>(proto);
        if (aodv)
        {
            currentStream += aodv->AssignStreams(currentStream);
            continue;
        }
        // MtcAodv may also be in a list
        Ptr<Ipv4ListRouting> list = DynamicCast<Ipv4ListRouting>(proto);
        if (list)
        {
            int16_t priority;
            Ptr<Ipv4RoutingProtocol> listProto;
            Ptr<mtcaodv::RoutingProtocol> listAodv;
            for (uint32_t i = 0; i < list->GetNRoutingProtocols(); i++)
            {
                listProto = list->GetRoutingProtocol(i, priority);
                listAodv = DynamicCast<mtcaodv::RoutingProtocol>(listProto);
                if (listAodv)
                {
                    currentStream += listAodv->AssignStreams(currentStream);
                    break;
                }
            }
        }
    }
    return (currentStream - stream);
}

} // namespace ns3
