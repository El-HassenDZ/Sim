/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This file is part of the MTC-AODV research prototype.
 */

#ifndef MTC_AODV_BLACKHOLE_AODV_HELPER_H
#define MTC_AODV_BLACKHOLE_AODV_HELPER_H

#include "ns3/attack-behavior.h"

#include "ns3/ipv4-routing-helper.h"
#include "ns3/node-container.h"
#include "ns3/object-factory.h"

#include <string>

namespace ns3
{
namespace mtcaodv
{

/**
 * @ingroup mtcaodv
 *
 * @brief Installs the AODV-interoperable malicious routing protocol on nodes.
 *
 * The helper follows the public contract of `ns3::AodvHelper` — `Copy`,
 * `Create`, `Set`, `AssignStreams` — so it can be handed to
 * `InternetStackHelper::SetRoutingHelper` exactly like the stock helper.
 *
 * It composes two independently configurable objects on each node: the
 * routing protocol, and the attack policy the protocol consults. Neither
 * knows the experiment's attacker list; the caller decides which nodes
 * receive this helper and which receive the stock `AodvHelper`.
 */
class BlackholeAodvHelper : public Ipv4RoutingHelper
{
  public:
    /** @brief Construct a helper producing default-configured attackers. */
    BlackholeAodvHelper();

    /**
     * @brief Clone this helper.
     * @return A newly allocated copy; the caller owns the memory.
     *
     * @internal Required by `Ipv4RoutingHelper` and used by
     * `InternetStackHelper`, which clones the helper it is given.
     */
    BlackholeAodvHelper* Copy() const override;

    /**
     * @brief Create the malicious routing protocol for one node.
     *
     * The attack policy is created from the configured factory and attached
     * to the protocol here, so a node installed through this helper is
     * malicious by construction and a node installed through `AodvHelper` is
     * not. This keeps ground truth in scenario configuration rather than in
     * any runtime component.
     *
     * @param node Node that will run the protocol.
     * @return The newly created routing protocol, already aggregated.
     */
    Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;

    /**
     * @brief Set an attribute on the routing protocol produced by this helper.
     * @param name Attribute name declared by `BlackholeAodvRoutingProtocol`.
     * @param value Attribute value.
     */
    void Set(std::string name, const AttributeValue& value);

    /**
     * @brief Select the attack-policy type produced for each node.
     * @param typeId Registered TypeId deriving from `AttackBehavior`.
     */
    void SetAttackBehaviorType(std::string typeId);

    /**
     * @brief Set an attribute on the attack policy produced by this helper.
     * @param name Attribute name declared by the configured policy type.
     * @param value Attribute value.
     */
    void SetAttackBehaviorAttribute(std::string name, const AttributeValue& value);

    /**
     * @brief Assign fixed random streams to the protocols installed by this helper.
     *
     * Mirrors `AodvHelper::AssignStreams`, including the list-routing case, so
     * paired experiments can reserve one stream range for malicious nodes and
     * a different range for honest ones.
     *
     * @param c Nodes whose protocols should use fixed streams.
     * @param stream First stream index to use.
     * @return Number of stream indices assigned.
     */
    int64_t AssignStreams(NodeContainer c, int64_t stream);

  private:
    /** @brief Factory producing the malicious routing protocol. */
    ObjectFactory m_agentFactory;

    /** @brief Factory producing the attack policy attached to each protocol. */
    ObjectFactory m_behaviorFactory;
};

} // namespace mtcaodv
} // namespace ns3

#endif // MTC_AODV_BLACKHOLE_AODV_HELPER_H
