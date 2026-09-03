/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This file is part of the MTC-AODV research prototype.
 */

#ifndef MTC_AODV_BLACKHOLE_BEHAVIOR_H
#define MTC_AODV_BLACKHOLE_BEHAVIOR_H

#include "attack-behavior.h"

#include "ns3/nstime.h"

#include <cstdint>
#include <string>

namespace ns3
{
namespace mtcaodv
{

/**
 * @ingroup mtcaodv
 *
 * @brief Deterministic full-Blackhole policy for the primary experiment.
 *
 * Once activated, the policy requests attractive forged RREPs and discards
 * transit data packets.  Routing and security control packets remain forwarded
 * by default so the attacker can continue participating in route discovery and
 * does not trivially isolate itself from the network.
 */
class BlackholeBehavior final : public AttackBehavior
{
  public:
    /**
     * @brief Return the ns-3 run-time type information for this policy.
     * @return The registered TypeId including all configurable attributes.
     */
    static TypeId GetTypeId();

    /**
     * @brief Construct the policy with conservative, documented defaults.
     *
     * The default attack starts after a 50-second warm-up, advertises a
     * one-hop route, adds 1000 to the observed destination sequence number,
     * and drops all transit data packets after activation.
     */
    BlackholeBehavior();

    /** @brief Destroy the Blackhole policy. */
    ~BlackholeBehavior() override;

    /** @copydoc AttackBehavior::GetBehaviorName() */
    std::string GetBehaviorName() const override;

    /** @copydoc AttackBehavior::IsActive() */
    bool IsActive(Time now) const override;

    /** @copydoc AttackBehavior::ShouldForgeRouteReply() */
    bool ShouldForgeRouteReply(Time now, bool hasUsableReverseRoute) const override;

    /** @copydoc AttackBehavior::CreateForgedReplyProfile() */
    ForgedReplyProfile CreateForgedReplyProfile(
        uint32_t observedDestinationSequenceNumber) const override;

    /** @copydoc AttackBehavior::ShouldDropTransitPacket() */
    bool ShouldDropTransitPacket(Time now,
                                 bool isRoutingControl,
                                 bool isSecurityControl) const override;

  private:
    /**
     * @brief Beginning of malicious activity, in simulation time.
     *
     * Symbol in the experiment specification: @f$t_{attack}@f$.
     */
    Time m_attackStartTime;

    /**
     * @brief Positive offset added to the RREQ destination sequence number.
     *
     * Symbol: @f$\Delta_{seq}@f$. Domain: [1, UINT32_MAX]. Unit: sequence
     * number increments.
     */
    uint32_t m_sequenceNumberOffset;

    /**
     * @brief Hop count advertised in each forged RREP.
     *
     * Symbol: @f$h_{fake}@f$. Domain: [0, 255]. Unit: hops.
     */
    uint8_t m_advertisedHopCount;

    /**
     * @brief Lifetime claimed for a forged route, in ns-3 time.
     *
     * Symbol: @f$T_{fake}@f$.
     */
    Time m_forgedRouteLifetime;

    /**
     * @brief Enables deterministic dropping of transit data after activation.
     */
    bool m_dropTransitData;

    /**
     * @brief Protects routing and security control traffic from attack drops.
     */
    bool m_preserveControlPlane;
};

} // namespace mtcaodv
} // namespace ns3

#endif // MTC_AODV_BLACKHOLE_BEHAVIOR_H
