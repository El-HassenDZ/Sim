/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This file is part of the MTC-AODV research prototype.
 */

#ifndef MTC_AODV_ATTACK_BEHAVIOR_H
#define MTC_AODV_ATTACK_BEHAVIOR_H

#include "ns3/nstime.h"
#include "ns3/object.h"

#include <cstdint>
#include <string>

namespace ns3
{
namespace mtcaodv
{

/**
 * @ingroup mtcaodv
 *
 * @brief Values that an attack policy asks the routing adapter to place in a
 *        forged AODV route reply.
 *
 * This structure contains policy outputs only.  It does not serialize an AODV
 * packet; that responsibility belongs to the routing adapter introduced in
 * the next integration gate.
 */
struct ForgedReplyProfile
{
    /**
     * @brief Advertised destination sequence number, expressed as an AODV
     *        unsigned sequence number.
     */
    uint32_t destinationSequenceNumber{0};

    /** @brief Advertised distance to the destination, measured in hops. */
    uint8_t hopCount{1};

    /** @brief Claimed validity period of the forged route, measured as ns-3 time. */
    Time routeLifetime{Seconds(0)};
};

/**
 * @ingroup mtcaodv
 *
 * @brief Complete context for one attacker drop decision.
 *
 * Every field is supplied by the routing adapter at the point of decision.
 * `isTransit` is the safety-critical one: it is true only for a packet that
 * this node is forwarding on behalf of another source-destination pair.
 */
struct PacketDropContext
{
    /** @brief Simulation time at which the decision is taken. */
    Time observationTime{Seconds(0)};

    /**
     * @brief True only when the node is forwarding for another endpoint pair.
     *
     * Defaults to false so that a caller which forgets to set it obtains the
     * benign decision rather than an unintended drop.
     */
    bool isTransit{false};

    /** @brief True for AODV routing-control traffic. */
    bool isRoutingControl{false};

    /** @brief True for MTC-AODV security-control traffic. */
    bool isSecurityControl{false};
};

/**
 * @ingroup mtcaodv
 *
 * @brief Abstract policy used by a malicious routing node.
 *
 * Separating attack policy from packet processing prevents the experiment
 * scenario from embedding attacker identities inside the detector.  A future
 * routing adapter may consult this interface, whereas all defensive components
 * must remain unaware of which nodes were configured as attackers.
 */
class AttackBehavior : public Object
{
  public:
    /**
     * @brief Return the ns-3 run-time type information for the policy base.
     * @return The registered TypeId.
     */
    static TypeId GetTypeId();

    /** @brief Construct an attack-policy interface. */
    AttackBehavior();

    /** @brief Destroy the attack-policy interface. */
    ~AttackBehavior() override;

    /**
     * @brief Return a stable human-readable behavior name for manifests.
     * @return A short policy name.
     */
    virtual std::string GetBehaviorName() const = 0;

    /**
     * @brief Determine whether malicious behavior is active at a given time.
     * @param now Current simulation time.
     * @return true when attack actions are permitted; false during the benign
     *         warm-up period.
     */
    virtual bool IsActive(Time now) const = 0;

    /**
     * @brief Decide whether the routing adapter may forge a route reply.
     * @param now Current simulation time.
     * @param hasUsableReverseRoute true when a valid reverse route can carry
     *        the reply back to the RREQ originator.
     * @return true only when the attack is active and a reply can actually be
     *         delivered to the originator.
     */
    virtual bool ShouldForgeRouteReply(Time now, bool hasUsableReverseRoute) const = 0;

    /**
     * @brief Build the malicious AODV fields requested by the policy.
     * @param observedDestinationSequenceNumber Sequence number learned from
     *        the received RREQ.
     * @return The values to be serialized by the routing adapter.
     */
    virtual ForgedReplyProfile CreateForgedReplyProfile(
        uint32_t observedDestinationSequenceNumber) const = 0;

    /**
     * @brief Decide whether a packet must be discarded by an attacker.
     *
     * The decision is expressed over a complete context rather than loose
     * arguments because the transit property is a safety precondition: an
     * attacker that discards its own locally originated or locally destined
     * traffic models a broken node, not a Blackhole, and the resulting loss
     * would be misattributed to the attack.  Making the property a required
     * field forces every call site to state it, and lets the policy refuse by
     * default when it is absent.
     *
     * @param context Complete decision context for one packet.
     * @return true when the packet is attack data and must be dropped.
     */
    virtual bool ShouldDropPacket(const PacketDropContext& context) const = 0;
};

} // namespace mtcaodv
} // namespace ns3

#endif // MTC_AODV_ATTACK_BEHAVIOR_H

