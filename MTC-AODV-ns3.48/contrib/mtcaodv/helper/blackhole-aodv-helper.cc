/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This file is part of the MTC-AODV research prototype.
 */

#include "blackhole-aodv-helper.h"

#include "ns3/blackhole-aodv-routing-protocol.h"
#include "ns3/blackhole-behavior.h"

#include "ns3/ipv4-list-routing.h"
#include "ns3/ipv4.h"
#include "ns3/node.h"

namespace ns3
{
namespace mtcaodv
{

BlackholeAodvHelper::BlackholeAodvHelper()
{
    m_agentFactory.SetTypeId("ns3::mtcaodv::BlackholeAodvRoutingProtocol");
    m_behaviorFactory.SetTypeId("ns3::mtcaodv::BlackholeBehavior");
}

BlackholeAodvHelper*
BlackholeAodvHelper::Copy() const
{
    return new BlackholeAodvHelper(*this);
}

Ptr<Ipv4RoutingProtocol>
BlackholeAodvHelper::Create(Ptr<Node> node) const
{
    Ptr<BlackholeAodvRoutingProtocol> agent =
        m_agentFactory.Create<BlackholeAodvRoutingProtocol>();

    // Composing the policy here, rather than inside the protocol, keeps the
    // protocol free of any attack parameter of its own and makes a node with
    // no policy behave as unmodified AODV.
    Ptr<AttackBehavior> behavior = m_behaviorFactory.Create<AttackBehavior>();
    agent->SetAttackBehavior(behavior);

    node->AggregateObject(agent);
    return agent;
}

void
BlackholeAodvHelper::Set(std::string name, const AttributeValue& value)
{
    m_agentFactory.Set(name, value);
}

void
BlackholeAodvHelper::SetAttackBehaviorType(std::string typeId)
{
    m_behaviorFactory.SetTypeId(typeId);
}

void
BlackholeAodvHelper::SetAttackBehaviorAttribute(std::string name, const AttributeValue& value)
{
    m_behaviorFactory.Set(name, value);
}

int64_t
BlackholeAodvHelper::AssignStreams(NodeContainer c, int64_t stream)
{
    int64_t currentStream = stream;

    // The loop mirrors AodvHelper::AssignStreams.  Its invariant is that
    // currentStream always points at the next unused index, so the returned
    // count is exactly the number of indices this helper consumed.
    for (auto i = c.Begin(); i != c.End(); ++i)
    {
        Ptr<Node> node = (*i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        NS_ASSERT_MSG(ipv4, "Ipv4 not installed on node");
        Ptr<Ipv4RoutingProtocol> proto = ipv4->GetRoutingProtocol();
        NS_ASSERT_MSG(proto, "Ipv4 routing not installed on node");

        Ptr<BlackholeAodvRoutingProtocol> blackhole =
            DynamicCast<BlackholeAodvRoutingProtocol>(proto);
        if (blackhole)
        {
            currentStream += blackhole->AssignStreams(currentStream);
            continue;
        }

        // The protocol may instead sit inside a routing list, which is how
        // InternetStackHelper composes it with static and global routing.
        Ptr<Ipv4ListRouting> list = DynamicCast<Ipv4ListRouting>(proto);
        if (list)
        {
            int16_t priority;
            for (uint32_t index = 0; index < list->GetNRoutingProtocols(); index++)
            {
                Ptr<Ipv4RoutingProtocol> listProto = list->GetRoutingProtocol(index, priority);
                Ptr<BlackholeAodvRoutingProtocol> listBlackhole =
                    DynamicCast<BlackholeAodvRoutingProtocol>(listProto);
                if (listBlackhole)
                {
                    currentStream += listBlackhole->AssignStreams(currentStream);
                    break;
                }
            }
        }
    }

    return (currentStream - stream);
}

} // namespace mtcaodv
} // namespace ns3
